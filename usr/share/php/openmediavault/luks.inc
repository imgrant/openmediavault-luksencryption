<?php
/**
 * This file is part of OpenMediaVault.
 *
 * @license   http://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    Volker Theile <volker.theile@openmediavault.org>
 * @author    OpenMediaVault Plugin Developers <plugins@omv-extras.org>
 * @copyright Copyright (c) 2009-2015 Volker Theile
 * @copyright Copyright (c) 2015 OpenMediaVault Plugin Developers
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
require_once("openmediavault/globals.inc");
require_once("openmediavault/object.inc");
require_once("openmediavault/functions.inc");
require_once("openmediavault/util.inc");

/**
 * Class for handling LUKS-encrypted disks.
 */
class OMVLuksContainers extends OMVObject {
	/**
	 * Enumerate LUKS devices.
	 * @return A list of devices that are LUKS containers, otherwise FALSE.
	 * Example: array(
	 *   0 => /dev/sdb
	 *   1 => /dev/sdd
	 * )
	 */
	public static function enumerate() {
		$cmd = "export LANG=C; lsblk -o kname,fstype -lbnr ".
		  "2>/dev/null ".
		  "| grep crypto_LUKS | awk '{print \"/dev/\"\$1}' ";
		@OMVUtil::exec($cmd, $output, $result);
		if($result !== 0)
			return FALSE;
		$list = array();
		// Parse command output:
		//   /dev/sdc
		//   /dev/sdd
		//   unknown device
		foreach($output as $outputk => $outputv) {
			$deviceFile = trim($outputv);
			if(!is_devicefile($deviceFile))
				continue;
			$list[] = $deviceFile;
		}
		return $list;
	}
}

/**
 * Class for handling a LUKS-encrypted device (aka a LUKS container).
 */
class OMVLuksContainer extends OMVStorageDeviceAbstract {

	protected $uuid = "";
	protected $isOpen = FALSE;
	protected $headerInfo = "";
	protected $usedKeySlots = 0;
	protected $freeKeySlots = 8;

	protected $deviceMapperDeviceFile = "";
	protected $deviceMapperName = "";

	private $dataCached = FALSE;

	/**
	 * Get the name of the device mapper device.
	 * @return The name of the device mapper device.
	 */
	public function getDeviceMapperName() {
		// Make sure the canonical device file is used to extract the name
		// of the device.
		$path = sprintf("/sys/block/%s/dm/name", $this->getDeviceMapperCanonicalName());
		if(!file_exists($path))
			return FALSE;
		return trim(file_get_contents($path));
	}

	/**
	 * Get the UUID of the device mapper device.
	 * @return The UUID of the device mapper device, otherwise FALSE.
	 */
	public function getDeviceMapperUuid() {
		// Make sure the canonical device file is used to extract the name
		// of the device.
		$path = sprintf("/sys/block/%s/dm/uuid", $this->getDeviceMapperCanonicalName());
		if(!file_exists($path))
			return FALSE;
		return trim(file_get_contents($path));
	}

	/**
	 * Get the holder device file of the container if it's open
	 * @return A device file string (/dev/dm-0), otherwise FALSE.
	 */
	public function getDeviceMapperDeviceFile() {
		if(FALSE === ($holders = $this->getHolders()))
			return FALSE;
		if(count($holders)!=1)
			return FALSE;
		return $holders[0];	// Should only be one holder, just return the first
	}

	/**
	 * Get the holder device name of the container if it's open
	 * @return A device name (dm-0), otherwise FALSE.
	 */
	public function getDeviceMapperCanonicalName() {
		// Get the device file and extract the name, e.g. /dev/sda => sda.
		if(FALSE === ($dev = $this->getDeviceMapperDeviceFile())) {
			return FALSE;
		}
		$deviceName = str_replace("/dev/", "", $dev);
		return $deviceName;
	}

	/**
	 * Get detailed information for the container.
	 * @private
	 * @return TRUE if successful, otherwise FALSE.
	 */
	private function getData() {
		if($this->dataCached !== FALSE)
			return TRUE;

		// Look up the UUID for the LUKS container
		$cmd = sprintf("export LANG=C; cryptsetup luksUUID %s",
				$this->getDeviceFile());
		@OMVUtil::exec($cmd, $output, $result);
		if($result !== 0) {
			$this->setLastError($output);
			return FALSE;
		}
		$uuid = trim($output[0]);
		$this->uuid = $uuid;
		unset($cmd, $output, $result);

		// Get size of device
		$this->size = $this->getSize();

		// Check if the device is open and get the device mapper name if so
		if (FALSE !== ($dev = $this->getDeviceMapperDeviceFile())) {
			// Any devices of the form /dev/dm-n are for internal use only and
			// should never be used. Because of that the device file returned
			// should look like /dev/mapper/<xyz>. See for more information:
			// https://access.redhat.com/site/documentation/en-US/Red_Hat_Enterprise_Linux/6/html-single/DM_Multipath
			if(1 == preg_match("/^\/dev\/dm-\d+$/", $dev)) {
				if(FALSE !== ($name = $this->getDeviceMapperName())) {
					$this->deviceMapperDeviceFile = sprintf("/dev/mapper/%s", $name);
					$this->deviceMapperName = $name;
				} else {
					$this->deviceMapperDeviceFile = $dev;
				}
			}
			$this->isOpen = TRUE;
		}

		// Collect the detailed output from luksDump
		$cmd = sprintf("export LANG=C; cryptsetup luksDump %s",
				$this->getDeviceFile());
		@OMVUtil::exec($cmd, $output, $result);
		if($result !== 0) {
			$this->setLastError($output);
			return FALSE;
		}
		$this->headerInfo = $output;
		$this->usedKeySlots = count(preg_grep("/^Key Slot \d: ENABLED$/", $this->headerInfo));
		$this->freeKeySlots = count(preg_grep("/^Key Slot \d: DISABLED$/", $this->headerInfo));
		unset($cmd, $output, $result);

		// Set flag to mark information has been successfully read.
		$this->dataCached = TRUE;

		return TRUE;
	}

	/**
	 * Refresh the cached information.
	 * @return TRUE if successful, otherwise FALSE.
	 */
	public function refresh() {
		$this->dataCached = FALSE;
		if($this->getData() === FALSE)
			return FALSE;
		return TRUE;
	}

	/**
	 * Checks if the container exists - synonym for isLuks().
	 * @return TRUE if the container exists, otherwise FALSE.
	 */
	public function exists() {
		return $this->isLuks();
	}

	/**
	 * Checks if the device exists and is a LUKS container
	 * @return TRUE if the device is a LUKS container, otherwise FALSE.
	 */
	public function isLuks() {
		if (FALSE === parent::exists())
		 	return FALSE;
		return self::isLuksContainer($this->getDeviceFile());
	}

	/**
	 * Helper function for determining if a device is a LUKS container.
	 * @return TRUE if the device is a LUKS container, otherwise FALSE.
	 */
	public static function isLuksContainer($deviceFile) {
		// TODO: would it be better (quicker) to check the UUID
		// in, e.g., /sys/block/dm-0/dm/uuid (it begins 'CRYPTO' for
		// LUKS/dm-crypt devices)?
		$cmd = sprintf("export LANG=C; cryptsetup isLuks %s",
					escapeshellarg($deviceFile));
		@OMVUtil::exec($cmd, $output, $result);
		if($result !== 0)
			return FALSE;
		return TRUE;
	}

	/**
	 * Get the holder devices of the LUKS container (i.e. the decrypted device,
	 * if open).
	 * @return An array of device files, otherwise FALSE.
	 */
	public function getHolders() {
		// Make sure the canonical device file is used to extract the name
		// of the device.
		$path = sprintf("/sys/block/%s/holders", $this->getDeviceName(TRUE));
		if(!file_exists($path))
			return FALSE;
		$result = array();
		$dir = new DirectoryIterator($path);
		foreach($dir as $item) {
			if($item->isDot() || !$item->isLink())
				continue;
			$result[] = sprintf("/dev/%s", $item->getFilename());
		}
		return $result;
	}

	/**
	 * Get detailed information about the container.
	 * @return Detailed information about the container, FALSE on failure.
	 */
	public function getDetail() {
		if($this->getData() === FALSE)
			return FALSE;
		return implode("\n", $this->headerInfo);
	}

	/**
	 * How many key slots are used.
	 * @return Number of used key slots, FALSE on failure.
	 */
	public function getUsedKeySlots() {
		if($this->getData() === FALSE)
			return FALSE;
		return $this->usedKeySlots;
	}

	/**
	 * How many key slots are unused.
	 * @return Number of free key slots, FALSE on failure.
	 */
	public function getFreeKeySlots() {
		if($this->getData() === FALSE)
			return FALSE;
		return $this->freeKeySlots;
	}

	/**
	 * Get the UUID of the container.
	 * @return The UUID of the container, FALSE on failure.
	 */
	public function getUuid() {
		if($this->getData() === FALSE)
			return FALSE;
		return $this->uuid;
	}

	/**
	 * Is the container open? (i.e. unlocked and mapped).
	 * @return TRUE if the container is open, otherwise FALSE.
	 */
	public function isOpen() {
		if($this->getData() === FALSE)
			return FALSE;
		return $this->isOpen;
	}

	/**
	 * Get the devicefile of the mapped device (i.e. the open container).
	 * @return The mapped devicefile of the container, FALSE on failure.
	 */
	public function getDecryptedDeviceFile() {
		if($this->getData() === FALSE)
			return FALSE;
		return $this->deviceMapperDeviceFile;
	}

	/**
	 * Get the device name of the mapped device (i.e. the open container).
	 * @return The mapped device name of the container, FALSE on failure.
	 */
	public function getDecryptedName() {
		if($this->getData() === FALSE)
			return FALSE;
		return $this->deviceMapperName;
	}

	/**
	 * Get the description of the LUKS container.
	 * @return The LUKS container description, FALSE on failure.
	 */
	public function getDescription() {
		if ($this->getData() === FALSE)
			return FALSE;
		return sprintf(gettext("LUKS encrypted device %s [%s, %s]"),
		  $this->getDecryptedName(), $this->getDeviceFile(),
		  binary_format($this->getSize()));
	}

	/**
	 * Create the container.
	 * @param params An array containing the following fields:
	 *	 \em devicefile The device file where to create the LUKS container.
	 *	 \em passphrase The passphrase to unlock the device.
	 * @return TRUE if successful, otherwise FALSE.
	 */
	public function create($params) {
		$cmd = sprintf("export LANG=C; echo -n %s ".
			"| cryptsetup luksFormat %s - 2>&1",
			escapeshellarg($params['passphrase']),
			escapeshellarg($params['devicefile']));
		@OMVUtil::exec($cmd, $output, $result);
		if ($result !== 0) {
			$this->setLastError($output);
			return FALSE;
		}
		$this->refresh();
		return TRUE;
	}

	/**
	 * Remove the container.
	 * @return TRUE if successful, otherwise FALSE.
	 */
	public function remove() {
		// First, close the device if it is open
		if ($this->isOpen()) {
			if (FALSE === $luks->close()) {
				return FALSE;
			}
		}
		// Get the payload offset (header size)
		$cmd = sprintf("export LANG=C; cryptsetup luksDump %s ".
			"| grep 'Payload offset' | awk '{print $3}'",
		  	escapeshellarg($this->getDeviceFile()));
		@OMVUtil::exec($cmd, $output, $result);
		if ($result !== 0) {
			$this->setLastError($output);
			// Don't quit if we can't work out the header size,
			// just assume a default of 2MiB (offset 4096) instead
			$header_size = 4096;
		} else {
			$header_size = trim($output[0]);
		}
		unset($cmd, $output, $result);

		// Get the storage device object.
  		$sd = OMVStorageDeviceFactory::get($this->getDeviceFile());
  		if (is_null($sd) || !$sd->exists()) {
			$this->setLastError($output);
  			return FALSE;
  		}
		// Get the storage device backend of the given device.
		$sdb = OMVStorageDevices::getBackend($sd->getDeviceFile());
		if (is_null($sdb)) {
			$this->setLastError($output);
			return FALSE;
		}
		switch ($sdb->getType()) {
		case OMV_STORAGE_DEVICE_TYPE_SOFTWARERAID:
		case OMV_STORAGE_DEVICE_TYPE_DEVICEMAPPER:
			// Wipe existing filesystems.
			$cmd = sprintf("export LANG=C; wipefs -a %s 2>&1",
			  escapeshellarg($sd->getDeviceFile()));
			@OMVUtil::exec($cmd, $output, $result);
  			if ($result !== 0) {
				$this->setLastError($output);
				return FALSE;
  			}
			break;
		default:
			// Wipe existing filesystems.
			$cmd = sprintf("export LANG=C; sgdisk --zap-all %s 2>&1",
			  escapeshellarg($sd->getDeviceFile()));
			@OMVUtil::exec($cmd, $output, $result);
			if ($result !== 0) {
				$this->setLastError($output);
				return FALSE;
			}
			break;
		}
		unset($cmd, $output, $result);
		// Destroy the header by overwriting it
		$cmd = sprintf("export LANG=C; dd if=/dev/urandom of=%s bs=512 count=%s 2>&1",
			escapeshellarg($sd->getDeviceFile()),
			escapeshellarg($header_size));
		@OMVUtil::exec($cmd, $output, $result);
		if ($result !== 0) {
			$this->setLastError($output);
			return FALSE;
		}
		$this->refresh();
		return TRUE;
	}

	/**
	 * Open (unlock) the container.
	 * @param passphrase A passphrase that will unlock the LUKS device.
	 * @return TRUE if successful, otherwise FALSE.
	 */
	public function open($passphrase) {
		$cmd = sprintf("export LANG=C; echo -n %s ".
				"| cryptsetup luksOpen %s %s-crypt --key-file=- 2>&1",
				escapeshellarg($passphrase),
				escapeshellarg($this->getDeviceFile()),
				escapeshellarg($this->getDeviceName()));
		@OMVUtil::exec($cmd, $output, $result);
		if ($result !== 0) {
			$this->setLastError($output);
			return FALSE;
		}
		$this->refresh();
		return TRUE;
	}

	/**
	 * Close (lock) the container.
	 * @return TRUE if successful, otherwise FALSE.
	 */
	public function close() {
		$cmd = sprintf("export LANG=C; cryptsetup luksClose %s 2>&1",
				escapeshellarg($this->getDecryptedName()));
		@OMVUtil::exec($cmd, $output, $result);
		if ($result !== 0) {
			$this->setLastError($output);
			return FALSE;
		}
		$this->refresh();
		return TRUE;
	}

	/**
	 * Add a key (passphrase) to the container.
	 * @param params An array containing the following fields:
	 *	 \em devicefile The device file representing the LUKS container.
	 *	 \em oldpassphrase A passphrase that unlocks the device.
	 *	 \em newpassphrase The new passphrase to add.
	 * @return TRUE if successful, otherwise FALSE.
	 */
	public function addKey($params) {
		$cmd = sprintf("/bin/bash -c 'export LANG=C; echo -n %s ".
			"| cryptsetup luksAddKey %s --key-file=- ".
			"<(echo -n %s) 2>&1'",
			escapeshellarg($params['oldpassphrase']),
			escapeshellarg($params['devicefile']),
			escapeshellarg($params['newpassphrase']));
		@OMVUtil::exec($cmd, $output, $result);
		if ($result !== 0) {
			$this->setLastError($output);
			return FALSE;
		}
		$this->refresh();
		return TRUE;
	}

	/**
	 * Change a key (passphrase) for the container.
	 * @param params An array containing the following fields:
	 *	 \em devicefile The device file representing the LUKS container.
	 *	 \em oldpassphrase The passphrase to unlock the device.
	 *	 \em newpassphrase The new passphrase to replace the old one with.
	 * @return TRUE if successful, otherwise FALSE.
	 */
	public function changeKey($params) {
		$cmd = sprintf("/bin/bash -c 'export LANG=C; echo -n %s ".
			"| cryptsetup luksChangeKey %s --key-file=- ".
			"<(echo -n %s) 2>&1'",
			escapeshellarg($params['oldpassphrase']),
			escapeshellarg($params['devicefile']),
			escapeshellarg($params['newpassphrase']));
		@OMVUtil::exec($cmd, $output, $result);
		if ($result !== 0) {
			$this->setLastError($output);
			return FALSE;
		}
		$this->refresh();
		return TRUE;
	}

	/**
	 * Remove a key (passphrase) from the container.
	 * @param params An array containing the following fields:
	 *	 \em devicefile The device file representing the LUKS container.
	 *	 \em passphrase The passphrase to remove.
	 * @return TRUE if successful, otherwise FALSE.
	 */
	public function removeKey($params) {
		$cmd = sprintf("/bin/bash -c 'export LANG=C; echo -n %s ".
			"| cryptsetup luksRemoveKey %s --key-file=- 2>&1'",
			escapeshellarg($params['passphrase']),
			escapeshellarg($params['devicefile']));
		@OMVUtil::exec($cmd, $output, $result);
		if ($result !== 0) {
			$this->setLastError($output);
			return FALSE;
		}
		$this->refresh();
		return TRUE;
	}
}


/**
 * Class for handling an open LUKS container,
 * i.e. from the decrypted device point of view.
 */
class OMVStorageDeviceLUKS extends OMVStorageDeviceDM {
	/**
	 * Get the base (slave) device file of the container
	 * @return A device file string (/dev/sdb), otherwise FALSE.
	 */
	public function getLuksEncryptedDeviceFile() {
		if(FALSE === ($slaves = $this->getSlaves()))
			return FALSE;
		if(count($slaves)!=1)
			return FALSE;
		return $slaves[0];	// Should only be one slave, just return the first
	}

	 /**
 	 * Get the canonical device file, e.g. /dev/disk/by-label/root -> /dev/sda
	 * @return A device file string (/dev/sda), otherwise FALSE.
 	 */
 	public function getCanonicalLuksEncryptedDeviceFile() {
		if(FALSE === ($dev = $this->getLuksEncryptedDeviceFile()))
			return FALSE;
 		return realpath($dev);
 	}

	/**
	 * Get the base (slave) device name of the container
	 * @param canonical If set to TRUE the canonical device file will
	 *   be used. Defaults to FALSE.
	 * @return A device name (sdb), otherwise FALSE.
	 */
	public function getLuksEncryptedDeviceName($canonical = FALSE) {
		$deviceName = str_replace("/dev/", "", !$canonical ?
		  $this->getLuksEncryptedDeviceFile() : $this->getCanonicalLuksEncryptedDeviceFile());
		return $deviceName;
	}

	/**
	 * Get the container (encrypted device) that backs
	 * (i.e. is the slave of) this decrypted (open) device
	 * @return An OMVLuksContainer object
	 */
	public function getContainer() {
		return new OMVLuksContainer($this->getLuksEncryptedDeviceFile());
	}

	/**
	 * Checks if the container exists - synonym for isLuks().
	 * @return TRUE if the container exists, otherwise FALSE.
	 */
	public function exists() {
		return $this->isLuks();
	}

	/**
	 * Checks if the device exists and is a LUKS container
	 * @return TRUE if the device is a LUKS container, otherwise FALSE.
	 */
	public function isLuks() {
		if(FALSE === ($dev = $this->getLuksEncryptedDeviceFile()))
			return FALSE;
		// Test whether the slave is a LUKS container or not
		return OMVLuksContainer::isLuksContainer($dev);
	}

	/**
	 * Get the description of the LUKS container.
	 * @return The LUKS container description, FALSE on failure.
	 */
	public function getDescription() {
		return sprintf(gettext("LUKS encrypted device %s [%s, %s]"),
		  $this->getLuksEncryptedDeviceName(), $this->getDeviceFile(),
		  binary_format($this->getSize()));
	}
}


/**
 * Implements the storage device backend for (open) LUKS containers.
 */
class OMVStorageDeviceBackendLUKS extends OMVStorageDeviceBackendDM {
	function enumerate() {
		$devs = parent::enumerate();
		if(FALSE === $devs)
			return FALSE;
		$result = array();
		foreach($devs as $devk => $devv) {
			// Check if the given device is a LUKS container.
			$object = new OMVStorageDeviceLUKS($devv);
			if(!$object->isLuks())
				continue;
			$result[] = $object->getDeviceFile();
		}
		return $result;
	}

	function isTypeOf($deviceFile) {
		// Check if the given device is of type device mapper.
		if(FALSE === parent::isTypeOf($deviceFile))
			return FALSE;
		// Check if it is an open LUKS container.
		$object = new OMVStorageDeviceLUKS($deviceFile);
		return $object->isLuks();
	}

	function getImpl($args) {
		return new OMVStorageDeviceLUKS($args);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Register new storage device backend.
// N.B. To work most effectively, this needs to be registered before the
// LVM and Device Mapper backends, otherwise open LUKS containers will be
// enumerated as plain Device Mapper devices.
///////////////////////////////////////////////////////////////////////////////
OMVStorageDevices::registerBackend(new OMVStorageDeviceBackendLUKS());
?>
