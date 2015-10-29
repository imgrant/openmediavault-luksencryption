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
// require("js/omv/WorkspaceManager.js")
// require("js/omv/workspace/grid/Panel.js")
// require("js/omv/workspace/window/Form.js")
// require("js/omv/workspace/window/Grid.js")
// require("js/omv/workspace/window/plugin/ConfigObject.js")
// require("js/omv/Rpc.js")
// require("js/omv/data/Store.js")
// require("js/omv/data/Model.js")
// require("js/omv/data/proxy/Rpc.js")
// require("js/omv/util/Format.js")
// require("js/omv/window/Execute.js")

/**
 * @class OMV.module.admin.storage.luks.container.Create
 * @derived OMV.workspace.window.Form
 */
Ext.define("OMV.module.admin.storage.luks.container.Create", {
	extend: "OMV.workspace.window.Form",
	requires: [
		"OMV.data.Store",
		"OMV.data.Model",
		"OMV.data.proxy.Rpc"
	],

	title: _("Create encrypted device"),
	okButtonText: _("OK"),
	hideResetButton: true,
	width: 500,
	rpcService: "LuksMgmt",
	rpcSetMethod: "createContainer",

	getFormItems: function() {
		var me = this;
		return [{
			xtype: "combo",
			name: "devicefile",
			fieldLabel: _("Device"),
			emptyText: _("Select a device ..."),
			store: Ext.create("OMV.data.Store", {
				autoLoad: true,
				model: OMV.data.Model.createImplicit({
					idProperty: "devicefile",
					fields: [
						{ name: "devicefile", type: "string" },
						{ name: "description", type: "string" }
					]
				}),
				proxy: {
					type: "rpc",
					appendSortParams: false,
					rpcData: {
						service: "LuksMgmt",
						method: "getContainerCandidates"
					}
				},
				sorters: [{
					direction: "ASC",
					property: "devicefile"
				}]
			}),
			displayField: "description",
			valueField: "devicefile",
			allowBlank: false,
			editable: false,
			triggerAction: "all"
		},{
			xtype: "passwordfield",
			name: "passphrase",
			fieldLabel: _("Passphrase"),
			allowBlank: false,
			triggerAction: "all"
		},{
			xtype: "passwordfield",
			name: "passphraseconf",
			fieldLabel: _("Confirm passphrase"),
			allowBlank: false,
			submitValue: false
		}];
	},

	isValid: function() {
		var me = this;
		if (!me.callParent(arguments))
			return false;
		var valid = true;
		var values = me.getValues();
		// Check the passphrases match.
		var field = me.findField("passphraseconf");
		if (values.passphrase !== field.getValue()) {
			var msg = _("Passphrases don't match");
			me.markInvalid([
				{ id: "passphrase", msg: msg },
				{ id: "passphraseconf", msg: msg }
			]);
			valid = false;
		}
		return valid;
	},

	doSubmit: function() {
		var me = this;
		OMV.MessageBox.show({
			title: _("Confirmation"),
			msg: _("Do you really want to encrypt this device? Any existing data on it will be deleted."),
			buttons: Ext.Msg.YESNO,
			fn: function(answer) {
				if(answer === "no")
					return;
				me.superclass.doSubmit.call(me);
			},
			scope: me,
			icon: Ext.Msg.QUESTION
		});
	}
});


/**
 * @class OMV.module.admin.storage.luks.container.Unlock
 * @derived OMV.workspace.window.Form
 * @param uuid The UUID of the configuration object.
 * @param devicefile The device file, e.g. /dev/sda.
 */
Ext.define("OMV.module.admin.storage.luks.container.Unlock", {
	extend: "OMV.workspace.window.Form",

	rpcService: "LuksMgmt",
	rpcSetMethod: "openContainer",
	title: _("Unlock encrypted device"),
	autoLoadData: false,
	hideResetButton: true,
	okButtonText: "Unlock",
	width: 450,

	getFormConfig: function() {
		return {
			layout: {
				type: "vbox",
				align: "stretch"
			}
		};
	},

	getFormItems: function() {
		var me = this;
		return [{
			xtype: "textfield",
			name: "devicefile",
			fieldLabel: _("Device"),
			allowBlank: false,
			readOnly: true,
			value: me.devicefile
		},{
			xtype: "passwordfield",
			name: "passphrase",
			fieldLabel: _("Passphrase"),
			allowBlank: false
		}];
	},

	getRpcSetParams: function() {
		var me = this;
		var params = me.callParent(arguments);
		return Ext.apply(params, {
			devicefile: me.devicefile
		});
	}
});


/**
 * @class OMV.module.admin.storage.luks.container.Detail
 * @derived OMV.workspace.window.TextArea
 */
Ext.define("OMV.module.admin.storage.luks.container.Detail", {
	extend: "OMV.workspace.window.TextArea",

	rpcService: "LuksMgmt",
	rpcGetMethod: "getContainerDetails",
	title: _("Encrypted device details"),
	width: 550,
	height: 450
});


/**
 * @class OMV.module.admin.storage.luks.Containers
 * @derived OMV.workspace.grid.Panel
 */
Ext.define("OMV.module.admin.storage.luks.Containers", {
	extend: "OMV.workspace.grid.Panel",
	requires: [
		"OMV.data.Store",
		"OMV.data.Model",
		"OMV.data.proxy.Rpc"
	],
	uses: [
		"OMV.module.admin.storage.luks.container.Create",
		"OMV.module.admin.storage.luks.container.Detail"
	],

	autoReload: true,
	rememberSelected: true,
	hideAddButton: true,
	hideEditButton: true,
	hidePagingToolbar: false,
	disableLoadMaskOnLoad: true,
	stateful: true,
	stateId: "5abd703b-5ec7-4248-9138-452db85d17d5",
	columns: [{
			xtype: "emptycolumn",
			text: _("Device"),
			sortable: true,
			dataIndex: "devicefile",
			stateId: "devicefile"
		},{
			xtype: "binaryunitcolumn",
			text: _("Size"),
			sortable: true,
			dataIndex: "size",
			stateId: "size"
		},{
			text: _("Unlocked"),
			sortable: true,
			dataIndex: "unlocked",
			stateId: "unlocked",
			width: 80,
			resizable: false,
			align: "center",
			renderer: function(value, metaData, record) {
				var iconCls;
				switch (record.get("unlockatboot")) {
				case 1:
				case true: // Device is in crypttab
					iconCls = (true == value) ?
					  "grid-cell-booleaniconcolumn-led-blue" :
					  "grid-cell-booleaniconcolumn-led-red";
					break;
				default: // Device is not in crypttab
					iconCls = (true == value) ?
					  "grid-cell-booleaniconcolumn-led-blue" :
					  "grid-cell-booleaniconcolumn-led-gray";
					break;
				}
				metaData.tdCls = Ext.baseCSSPrefix +
				  "grid-cell-booleaniconcolumn" + " " +
				  Ext.baseCSSPrefix + iconCls;
				return "";
			}
		},{
			text: _("Decrypted device"),
			sortable: true,
			dataIndex: "decrypteddevicefile",
			stateId: "decrypteddevicefile",
			renderer: function(value) {
				if (!value || 0 === value.length) {
					value = _("n/a");
				}
				return value;
			}
		},{
			xtype: "booleantextcolumn",
			text: _("Referenced"),
			sortable: true,
			dataIndex: "_used",
			stateId: "_used",
			renderer: function(value) {
				if (!value || 0 === value.length) {
					value = _("n/a");
				}
				return value;
			}
		}],

	initComponent: function() {
		var me = this;
		Ext.apply(me, {
			store: Ext.create("OMV.data.Store", {
				autoLoad: true,
				model: OMV.data.Model.createImplicit({
					// Note, do not use 'devicefile' as idProperty, because
					// it is not guaranteed that the devicefile is set. This
					// is the case when a device is configured for mounting
					// but does not exist (e.g. USB).
					identifier: "uuid", // Populate 'id' field automatically.
					idProperty: "id",
					fields: [
						{ name: "id", type: "string", persist: false },
						{ name: "uuid", type: "string" },
						{ name: "devicefile", type: "string" },
						{ name: "size", type: "string" },
						{ name: "unlocked", type: "boolean" },
						{ name: "decrypteddevicefile", type: "string" },
						{ name: "_used", type: "boolean" }
					]
				}),
				proxy: {
					type: "rpc",
					rpcData: {
						service: "LuksMgmt",
						method: "getContainersList",
						options: {
							updatelastaccess: false
						}
					}
				},
				remoteSort: true,
				sorters: [{
					direction: "ASC",
					property: "devicefile"
				}]
			})
		});
		me.callParent(arguments);
	},

	getTopToolbarItems: function() {
		var me = this;
		var items = me.callParent(arguments);
		Ext.Array.insert(items, 1, [{
			id: me.getId() + "-create",
			xtype: "button",
			text: _("Create"),
			icon: "images/add.svg",
			iconCls: Ext.baseCSSPrefix + "btn-icon-16x16",
			handler: Ext.Function.bind(me.onCreateButton, me, [ me ]),
			scope: me,
			disabled: false
		},{
			id: me.getId() + "-unlock",
			xtype: "button",
			text: _("Unlock"),
			icon: "images/padlock-open.svg",
			iconCls: Ext.baseCSSPrefix + "btn-icon-16x16",
			handler: Ext.Function.bind(me.onUnlockButton, me, [ me ]),
			scope: me,
			disabled: true
		},{
			id: me.getId() + "-lock",
			xtype: "button",
			text: _("Lock"),
			icon: "images/padlock-closed.svg",
			iconCls: Ext.baseCSSPrefix + "btn-icon-16x16",
			handler: Ext.Function.bind(me.onLockButton, me, [ me ]),
			scope: me,
			disabled: true
		},{
			id: me.getId() + "-detail",
			xtype: "button",
			text: _("Detail"),
			icon: "images/details.svg",
			iconCls: Ext.baseCSSPrefix + "btn-icon-16x16",
			handler: me.onDetailButton,
			scope: me,
			disabled: true
		}]);
		return items;
	},

	onSelectionChange: function(model, records) {
		var me = this;
		me.callParent(arguments);
		// Process additional buttons.
		var tbarBtnDisabled = {
			"delete": true,
			"unlock": true,
			"lock": true,
			"detail": true
		};
		if (records.length <= 0) {
			tbarBtnDisabled["delete"] = true;
			tbarBtnDisabled["unlock"] = true;
			tbarBtnDisabled["lock"] = true;
			tbarBtnDisabled["detail"] = true;
		} else if(records.length == 1) {
			var record = records[0];
			// Set default values.
			tbarBtnDisabled["delete"] = true;
			tbarBtnDisabled["unlock"] = true;
			tbarBtnDisabled["lock"] = true;
			tbarBtnDisabled["detail"] = false;
			// Disable/enable the unlock/lock buttons depending on whether
			// the selected device is open.
			if (true === record.get("unlocked")) {
				tbarBtnDisabled["lock"] = false;
				tbarBtnDisabled["delete"] = true;
			} else {
				tbarBtnDisabled["unlock"] = false;
				tbarBtnDisabled["delete"] = false;
				// Disable the 'Unlock' button if the device does not
				// provide a UUID.
				if(Ext.isEmpty(record.get("uuid"))) {
					tbarBtnDisabled["unlock"] = true;
				}
			}
			// If the device is in use, then also disable the lock
			// button.
			if (true === record.get("_used"))
				tbarBtnDisabled["lock"] = true;
		} else {
			// Set default values.
			tbarBtnDisabled["delete"] = false;
			tbarBtnDisabled["unlock"] = true;
			tbarBtnDisabled["lock"] = true;
			tbarBtnDisabled["detail"] = true;
		}
		// Disable 'Delete' button if a selected device is in use or unlocked
		for (var i = 0; i < records.length; i++) {
			if (true == records[i].get("_used")) {
				tbarBtnDisabled["delete"] = true;
			}
			if (true == records[i].get("unlocked")) {
				tbarBtnDisabled["delete"] = true;
			}
		}
		// Update the button controls.
		Ext.Object.each(tbarBtnDisabled, function(key, value) {
			this.setToolbarButtonDisabled(key, value);
		}, me);
	},

	onCreateButton: function() {
		var me = this;
		Ext.create("OMV.module.admin.storage.luks.container.Create", {
			listeners: {
				scope: me,
				submit: function() {
					this.doReload();
				}
			}
		}).show();
	},

	onUnlockButton: function() {
		var me = this;
		var record = me.getSelected();
		Ext.create("OMV.module.admin.storage.luks.container.Unlock", {
			uuid: record.get("uuid"),
			devicefile: record.get("devicefile"),
			listeners: {
				scope: me,
				submit: function() {
					this.doReload();
				}
			}
		}).show();
	},

	onLockButton: function() {
		var me = this;
		var record = me.getSelected();
		var df = record.get("devicefile");
		// Execute RPC.
		OMV.Rpc.request({
			scope: me,
			callback: function(df, success, response) {
				this.doReload();
			},
			relayErrors: false,
			rpcData: {
				service: "LuksMgmt",
				method: "closeContainer",
				params: {
					devicefile: df
				}
			}
		});
	},

	onItemDblClick: function() {
		var me = this;
		me.onDetailButton(me);
	},

	onDetailButton: function() {
		var me = this;
		var record = me.getSelected();
		Ext.create("OMV.module.admin.storage.luks.container.Detail", {
			rpcGetParams: {
				devicefile: record.get("devicefile")
			}
		}).show();
	},

	startDeletion: function(records) {
		var me = this;
		if(records.length <= 0)
			return;
		OMV.MessageBox.show({
			title: _("Delete encrypted device"),
			msg: _("Do you really want to delete the encrypted device?<br/>The encryption key will be destroyed and all data will be lost."),
			icon: Ext.Msg.WARNING,
			buttonText: {
				yes: _("No"),
				no: _("Yes")
			},
			scope: me,
			fn: function(answer) {
				switch(answer) {
				case "no": // Attention, switched buttons.
					me.superclass.startDeletion.apply(this, [ records ]);
					break;
				default:
					break;
				}
			}
		});
	},

	doDeletion: function(record) {
		var me = this;
		var df = record.get("devicefile");
		// Execute RPC.
		OMV.Rpc.request({
			scope: me,
			callback: me.onDeletion,
			rpcData: {
				service: "LuksMgmt",
				method: "deleteContainer",
				params: {
					devicefile: df
				}
			}
		});
	}
});


OMV.WorkspaceManager.registerPanel({
	id: "containers",
	path: "/storage/luks",
	text: _("Encrypted Devices"),
	position: 10,
	className: "OMV.module.admin.storage.luks.Containers"
});
