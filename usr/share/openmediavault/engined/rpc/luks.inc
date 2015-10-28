<?php
/**
 * This file is part of OpenMediaVault.
 *
 * @license   http://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    Volker Theile <volker.theile@openmediavault.org>
 * @copyright Copyright (c) 2009-2015 Volker Theile
 *
 * OpenMediaVault is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OpenMediaVault is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenMediaVault. If not, see <http://www.gnu.org/licenses/>.
 */
require_once("openmediavault/object.inc");
require_once("openmediavault/error.inc");
require_once("openmediavault/system.inc");
require_once("openmediavault/functions.inc");
require_once("openmediavault/luks.inc");
require_once("openmediavault/rpcservice.inc");
require_once("openmediavault/notify.inc");

class OMVRpcServiceLuksMgmt extends OMVRpcServiceAbstract {
	/**
	 * Get the RPC service name.
	 */
	public function getName() {
		return "LuksMgmt";
	}

	/**
	 * Initialize the RPC service.
	 */
	public function initialize() {
		$this->registerMethod("enumerateContainers");
		$this->registerMethod("getContainersList");
		$this->registerMethod("getContainerCandidates");
		$this->registerMethod("getContainerDetails");
		$this->registerMethod("createContainer");
		$this->registerMethod("deleteContainer");
		$this->registerMethod("modifyContainer");
		$this->registerMethod("openContainer");
		$this->registerMethod("closeContainer");
	}

	/**
	 * Enumerate all LUKS containers on the system.
	 * @param params The method parameters.
	 * @param context The context of the caller.
	 * @return An array of objects. Each object represents a LUKS container
	 *   with the following properties:
	 *   device file, uuid, size, status
	 * @throw E_EXEC_MISC
	 */
	public function enumerateContainers($params, $context) {
		// Validate the RPC caller context.
		$this->validateMethodContext($context, array(
			  "role" => OMV_ROLE_ADMINISTRATOR
		  ));
		// Enumerate all LUKS containers on the system.
		if (FALSE === ($devs = OMVLuksContainers::enumerate())) {
			throw new OMVException(OMVErrorMsg::E_EXEC_MISC,
			  "Failed to get list of encrypted devices");
		}
		$result = array();
		foreach($devs as $devk => $devv) {
			// Get the container details.
			$luks = new OMVLuksContainer($devv);
			if(!$luks->exists())
				continue;
			$result[] = array(
				"devicefile" => $luks->getDeviceFile(),
				"uuid" => $luks->getUuid(),
				"size" => $luks->getSize(),
				"unlocked" => $luks->isOpen(),
				"decrypteddevicefile" => $luks->getDecryptedDeviceFile()
			);
		}
		return $result;
	}

	/**
	 * Get a list of LUKS containers.
	 * @param params An array containing the following fields:
	 *   \em start The index where to start.
	 *   \em limit The number of objects to process.
	 *   \em sortfield The name of the column used to sort.
	 *   \em sortdir The sort direction, ASC or DESC.
	 * @param context The context of the caller.
	 * @return An array containing the requested objects. The field \em total
	 *   contains the total number of objects, \em data contains the object
	 *   array. An exception will be thrown in case of an error.
	 */
	function getContainersList($params, $context) {
		// Validate the RPC caller context.
		$this->validateMethodContext($context, array(
			  "role" => OMV_ROLE_ADMINISTRATOR
		  ));
		// Validate the parameters of the RPC service method.
		$this->validateMethodParams($params, '{
			  "type":"object",
			  "properties":{
				  "start":{"type":"integer"},
				  "limit":{'.$GLOBALS['OMV_JSONSCHEMA_COUNTFIELD'].'},
				  "sortfield":{'.$GLOBALS['OMV_JSONSCHEMA_SORTFIELD'].'},
				  "sortdir":{'.$GLOBALS['OMV_JSONSCHEMA_SORTDIR'].'}
			  }
		  }');
		// Enumerate all LUKS containers on the system.
		$containers = $this->callMethod("enumerateContainers", NULL, $context);
		foreach($containers as $luksk => &$luksv) {
			$used = FALSE;
			// Does the logical volume contain a filesystem and is it used?
			if(FALSE !== OMVRpc::exec("FsTab", "getByFsName", array(
			  "id" => $luksv['decrypteddevicefile']), $context)) {
				$used = TRUE;
			}
			$luksv['_used'] = $used;
		}
		// Filter result.
		return $this->applyFilter($containers, $params['start'], $params['limit'],
		  $params['sortfield'], $params['sortdir']);
	}

	/**
	 * Get list of devices that can be used to create a LUKS container.
	 * @param params The method parameters.
	 * @param context The context of the caller.
	 * @return An array containing objects with the following fields:
	 *   devicefile, size and description.
	 * @throw E_EXEC_MISC
	 */
	public function getContainerCandidates($params, $context) {
		global $xmlConfig;
		// Validate the RPC caller context.
		$this->validateMethodContext($context, array(
			  "role" => OMV_ROLE_ADMINISTRATOR
		  ));
		// Get a list of all potential usable devices.
		if (FALSE === ($devs = OMVStorageDevices::enumerateUnused())) {
			throw new OMVException(OMVErrorMsg::E_EXEC_MISC,
			  "Failed to get list of devices");
		}
		// Prepare the result list.
		$result = array();
		foreach ($devs as $devk => $devv) {
			// Get the object that implements the given storage device.
			$sd = OMVStorageDeviceFactory::get($devv);
			if (is_null($sd) || !$sd->exists())
				continue;
			// Skip read-only devices like CDROM.
			if (TRUE === $sd->isReadOnly())
				continue;
			// Check if device is referenced/used by a plugin.
			$xpath = sprintf("//services/devicefile[contains(.,'%s')]",
			  $sd->getDeviceFile());
			if (TRUE === $xmlConfig->exists($xpath))
				continue;
			// Does this device already contain a filesystem?
			if (FALSE !== OMVFilesystem::hasFileSystem($sd->getDeviceFile()))
				continue;
			// Is the device an already open LUKS container?
			// Don't allow LUKS-on-LUKS
			$luks = new OMVStorageDeviceLUKS($sd->getDeviceFile());
			if (TRUE === $luks->isLuks())
			 	continue;
			// The device is a potential candidate
			// to be used as a LUKS container.
			$result[] = array(
				"devicefile" => $sd->getDeviceFile(),
				"size" => $sd->getSize(),
				"description" => $sd->getDescription()
			);
		}
		return $result;
	}

	/**
	 * Get detail about a LUKS device.
	 * @param params An array containing the following fields:
	 *   \em devicefile The LUKS device file to get details from.
	 * @param context The context of the caller.
	 * @return The details of the given LUKS device as string.
	 * @throw E_MISC_DEVICE_NOT_FOUND
	 */
	public function getContainerDetails($params, $context) {
		// Validate the RPC caller context.
		$this->validateMethodContext($context, array(
			  "role" => OMV_ROLE_ADMINISTRATOR
		  ));
		// Validate the parameters of the RPC service method.
		$this->validateMethodParams($params, '{
			  "type":"object",
			  "properties":{
				  "devicefile":{'.$GLOBALS['OMV_JSONSCHEMA_DEVICEFILE'].'}
			  }
		  }');
		// Get software RAID device details.
		$luks = new OMVLuksContainer($params['devicefile']);
		if(FALSE === $luks->exists()) {
			throw new OMVException(OMVErrorMsg::E_MISC_DEVICE_NOT_FOUND,
			sprintf(gettext("LUKS container on '%s' not found"),
			$params['devicefile']));
		}
		return $luks->getDetail();
	}

	/**
	 * Open (unlock) a LUKS container.
	 * @param params An array containing the following fields:
 	 *   \em devicefile The block special device of the LUKS container to open.
	 * @param context The context of the caller.
	 * @return None
	 * @throw E_MISC_FAILURE
	 */
	public function openContainer($params, $context) {
		global $xmlConfig;
		// Validate the RPC caller context.
		$this->validateMethodContext($context, array(
			  "role" => OMV_ROLE_ADMINISTRATOR
		  ));
		// Validate the parameters of the RPC service method.
  		$this->validateMethodParams($params, '{
  			  "type":"object",
  			  "properties":{
  				  "devicefile":{'.$GLOBALS['OMV_JSONSCHEMA_DEVICEFILE'].'},
				  "passphrase":{"type":"string"}
  			  }
  		  }');
		// Validate the container
		$luks = new OMVLuksContainer($params['devicefile']);
		if (is_null($luks) || !$luks->exists()) {
			throw new OMVException(OMVErrorMsg::E_MISC_FAILURE,
			  sprintf(gettext("LUKS container on '%s' not found"),
			  $params['devicefile']));
		}
		// Check that the container is not already open, then use
		// the supplied passphrase to unlock it if not
		if (FALSE === $luks->isOpen()) {
			if (FALSE === $luks->open($params['passphrase'])) {
				throw new OMVException(OMVErrorMsg::E_MISC_FAILURE,
					sprintf(gettext("Unable to unlock encrypted device: %s"),
						$luks->getLastError()));
			}
		}
	}

	/**
	 * Close (lock) a LUKS container.
	 * @param params An array containing the following fields:
 	 *   \em devicefile The block special device of the LUKS container to close.
	 * @param context The context of the caller.
	 * @return None.
	 * @throw E_MISC_FAILURE
	 */
	public function closeContainer($params, $context) {
		global $xmlConfig;
		// Validate the RPC caller context.
		$this->validateMethodContext($context, array(
			  "role" => OMV_ROLE_ADMINISTRATOR
		  ));
		// Validate the parameters of the RPC service method.
  		$this->validateMethodParams($params, '{
  			  "type":"object",
  			  "properties":{
  				  "devicefile":{'.$GLOBALS['OMV_JSONSCHEMA_DEVICEFILE'].'}
  			  }
  		  }');
		// Validate the container
		$luks = new OMVLuksContainer($params['devicefile']);
		if (is_null($luks) || !$luks->exists()) {
			throw new OMVException(OMVErrorMsg::E_MISC_FAILURE,
			  sprintf(gettext("LUKS container on '%s' not found"),
			  $params['devicefile']));
		}
		// Check if the container is open, and proceed to close it if so
		if (TRUE === $luks->isOpen()) {
			if (FALSE === $luks->close()) {
				throw new OMVException(OMVErrorMsg::E_MISC_FAILURE,
				  sprintf(gettext("Unable to lock encrypted device: %s"),
				  	$luks->getLastError()));
			}
		}
	}

	/**
	 * Create a LUKS container.
	 * @param params An array containing the following fields:
	 *   \em devicefile The device file where to create the LUKS container.
	 *	 \em passphrase The passphrase to unlock the device.
	 * @param context The context of the caller.
	 * @return None.
	 * @throw E_MISC_DEVICE_NOT_FOUND
	 * @throw E_EXEC_FAILED
	 */
	public function createContainer($params, $context) {
		// Validate the RPC caller context.
		$this->validateMethodContext($context, array(
			  "role" => OMV_ROLE_ADMINISTRATOR
		  ));
		// Validate the parameters of the RPC service method.
		$this->validateMethodParams($params, '{
			  "type":"object",
			  "properties":{
				  "devicefile":{'.$GLOBALS['OMV_JSONSCHEMA_DEVICEFILE'].'},
				  "passphrase":{"type": "string"}
			  }
		  }');
		// Get the storage device object.
  		$sd = OMVStorageDeviceFactory::get($params['devicefile']);
  		if (is_null($sd) || !$sd->exists()) {
  			throw new OMVException(OMVErrorMsg::E_MISC_DEVICE_NOT_FOUND,
  			  $params['devicefile']);
  		}
		// Get the storage device backend of the given device.
		$sdb = OMVStorageDevices::getBackend($sd->getDeviceFile());
		if (is_null($sdb)) {
			throw new OMVException(OMVErrorMsg::E_MISC_FAILURE, sprintf(
			  "No storage device backend exists for device %s",
			  $sd->getDeviceFile()));
		}
		switch ($sdb->getType()) {
		case OMV_STORAGE_DEVICE_TYPE_SOFTWARERAID:
		case OMV_STORAGE_DEVICE_TYPE_DEVICEMAPPER:
			// Wipe existing filesystems.
			$cmd = sprintf("export LANG=C; wipefs -a %s 2>&1",
			  $sd->getDeviceFile());
			@OMVUtil::exec($cmd, $output, $result);
  			if ($result !== 0) {
				throw new OMVException(OMVErrorMsg::E_EXEC_FAILED,
				  $cmd, implode("\n", $output));
  			}
			break;
		default:
			// Wipe existing filesystems.
			$cmd = sprintf("export LANG=C; sgdisk --zap-all %s 2>&1",
			  escapeshellarg($sd->getDeviceFile()));
			@OMVUtil::exec($cmd, $output, $result);
			if ($result !== 0) {
				throw new OMVException(OMVErrorMsg::E_EXEC_FAILED,
				  $cmd, implode("\n", $output));
			}
			break;
		}
		// Reread partition table.
		$cmd = sprintf("export LANG=C; partprobe %s", escapeshellarg(
		  $sd->getDeviceFile()));
		@OMVUtil::exec($cmd, $output, $result);
		if ($result !== 0) {
		  throw new OMVException(OMVErrorMsg::E_EXEC_FAILED,
			$cmd, implode("\n", $output));
		}
		// Create the container.
		$luks = new OMVLuksContainer($sd->getDeviceFile());
		if (!$luks->create($params)) {
			throw new OMVException(OMVErrorMsg::E_EXEC_MISC, sprintf(
			  gettext("Failed to create encrypted device: %s"),
			  $luks->getLastError()));
		}
		// Notify configuration changes.
		$dispatcher = &OMVNotifyDispatcher::getInstance();
		$dispatcher->notify(OMV_NOTIFY_CREATE,
		  "org.openmediavault.system.storage.luks.container", array(
			  "devicefile" => $params['devicefile']
		  ));
	}

	/**
	 * Delete a LUKS container.
	 * @param params An array containing the following fields:
	 *   \em devicefile The devicefile of the LUKS device.
	 * @param context The context of the caller.
	 * @return None.
	 * @throw E_MISC_FAILURE
	 * @throw E_EXEC_MISC
	 */
	public function deleteContainer($params, $context) {
		// Validate the RPC caller context.
		$this->validateMethodContext($context, array(
			  "role" => OMV_ROLE_ADMINISTRATOR
		  ));
		// Validate the parameters of the RPC service method.
		$this->validateMethodParams($params, '{
			  "type":"object",
			  "properties":{
				  "devicefile":{'.$GLOBALS['OMV_JSONSCHEMA_DEVICEFILE'].'}
			  }
		  }');
		// Check if the container exists.
		$luks = new OMVLuksContainer($params['devicefile']);
		if (!$luks->exists()) {
			throw new OMVException(OMVErrorMsg::E_MISC_FAILURE,
			  sprintf(gettext("No encryption found on '%s'"),
			  $params['devicefile']));
		}
		// Remove the container.
		if (!$luks->remove()) {
			throw new OMVException(OMVErrorMsg::E_EXEC_MISC, sprintf(
			  gettext("Failed to remove the encrypted device: %s"),
			  $luks->getLastError()));
		}
		// Notify configuration changes.
		$dispatcher = &OMVNotifyDispatcher::getInstance();
		$dispatcher->notify(OMV_NOTIFY_DELETE,
		  "org.openmediavault.system.storage.luks.container", array(
			  "devicefile" => $params['devicefile']
		  ));
	}

	/**
	 * Modify a LUKS container.
	 * @param params An array containing the following fields:
	 *   \em devicefile The device file of the logical volume to modify.
	 *   \em openatboot A boolean value representing whether to unlock
	 *       the encrypted device at boot or not.
	 * @param context The context of the caller.
	 * @return None.
	 * @throw E_MISC_FAILURE
	 * @throw E_EXEC_FAILED
	 */
	public function modifyContainer($params, $context) {
		// Validate the RPC caller context.
		$this->validateMethodContext($context, array(
			  "role" => OMV_ROLE_ADMINISTRATOR
		  ));
		// Validate the parameters of the RPC service method.
		$this->validateMethodParams($params, '{
			  "type":"object",
			  "properties":{
				  "devicefile":{'.$GLOBALS['OMV_JSONSCHEMA_DEVICEFILE'].'},
				  "openatboot":{"type":"boolean"}
			  }
		  }');
		// Check if logical volume exists.
		$luks = new OMVLuksContainer($params['devicefile']);
		if (FALSE === $luks->exists()) {
			throw new OMVException(OMVErrorMsg::E_MISC_FAILURE,
			  sprintf("No encryption found on '%s'",
			  $params['devicefile']));
		}
		// Set the unlock at boot status
		// TODO: change crypttab method
		// Notify configuration changes.
		$dispatcher = &OMVNotifyDispatcher::getInstance();
		$dispatcher->notify(OMV_NOTIFY_MODIFY,
		  "org.openmediavault.system.storage.luks.container", array(
			  "devicefile" => $params['devicefile'],
			  "openatboot" => array_boolval($params, "openatboot")
		  ));
	}
}

// Register the RPC service.
$rpcServiceMgr = &OMVRpcServiceMgr::getInstance();
$rpcServiceMgr->registerService(new OMVRpcServiceLuksMgmt());
?>
