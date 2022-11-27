function changeTz() {
	var sel = document.getElementById("select_tz").value;
	document.getElementById("txt_timezone").value = sel;
}

function submitForm(id) {
	const f = document.forms.namedItem(id)
	if (f !== null) {
		f.submit();
	}
}

function selectListValue(selectName, currentValue) {
	var mySelect = document.getElementById(selectName);
	var opts = mySelect.options;
	
	for (var opt, j = 0; opt = opts[j]; j++) {
		if (opt.value == currentValue) {
			mySelect.selectedIndex = j;
			break;
		}
	}
}

function setInputValue(isEnabled, idOn, idOff) {
	if (isEnabled) {
		document.getElementById(idOn).checked = true;
		document.getElementById(idOff).checked = false;
	} else {
		document.getElementById(idOn).checked = false;
		document.getElementById(idOff).checked = true;
	}
}

// Set the Select dropdown value from the current value
selectListValue("select_printermodel1", current_printer);
selectListValue("select_hsioindex", current_hsioindex);

// Set the non-list values in the form
setInputValue(current_printer_enabled == 1, "printer-virt-yes", "printer-virt-no");
setInputValue(current_modem_enabled == 1, "modem-virt-yes", "modem-virt-no");
setInputValue(current_modem_sniffer_enabled == 1, "modem-sniffer-yes", "modem-sniffer-no");
setInputValue(current_cassette_enabled == 1, "pr-virt-yes", "pr-virt-no");
setInputValue(current_play_record == "0 PLAY", "pr-mode-play", "pr-mode-rec");
setInputValue(current_pulldown == "0 B Button Press", "pr-act-b", "pr-act-pull");
setInputValue(current_rotation_sounds == 1, "sam-rot-yes", "sam-rot-no");
setInputValue(current_boot_mode == 0, "boot-config-mode-config", "boot-config-mode-mount");
setInputValue(current_status_wait_enabled == 1, "boot-sio-wait-yes", "boot-sio-wait-no");
setInputValue(current_config_enabled == 1, "boot-config-disk-yes", "boot-config-disk-no");

// Printer PORT is triple value.
if (current_printerport == "1") {
	document.getElementById("printer-port-p1").checked = true;
} else if (current_printerport == "2") {
	document.getElementById("printer-port-p2").checked = true;
} else if (current_printerport == "3") {
	document.getElementById("printer-port-p3").checked = true;
} else {
	console.log("ERROR: unknown current_printer value:", current_printerport);
}
