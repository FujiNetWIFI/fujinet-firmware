function changeTz() {
	const selElement = document.getElementById("select_tz");
	const setElement = document.getElementById("txt_timezone")
	if (selElement == null || setElement == null) return;

	setElement.value = selElement.value;
}

function submitForm(id) {
	const f = document.forms.namedItem(id)
	if (f !== null) {
		f.submit();
	}
}

function selectListValue(selectName, currentValue) {
	var mySelect = document.getElementById(selectName);
	if (mySelect == null) return;

	var opts = mySelect.options;
	
	for (var opt, j = 0; opt = opts[j]; j++) {
		if (opt.value == currentValue) {
			mySelect.selectedIndex = j;
			break;
		}
	}
}

function setInputValue(isEnabled, idOn, idOff) {
	const onElement = document.getElementById(idOn)
	const offElement = document.getElementById(idOff)
	if (onElement == null || offElement == null) return;

	if (isEnabled) {
		document.getElementById(idOn).checked = true;
		document.getElementById(idOff).checked = false;
	} else {
		document.getElementById(idOn).checked = false;
		document.getElementById(idOff).checked = true;
	}
}

function setPrinterPort(printer_port) {
	// Printer PORT is triple value.
	const pp1E = document.getElementById("printer-port-p1");
	const pp2E = document.getElementById("printer-port-p2");
	const pp3E = document.getElementById("printer-port-p3");
	if (pp1E == null || pp2E == null || pp3E == null) return;

	if (printer_port == "1") {
		pp1E.checked = true;
	} else if (printer_port == "2") {
		pp2E.checked = true;
	} else if (printer_port == "3") {
		pp3E.checked = true;
	} else {
		console.log("ERROR: unknown printer_port value:", printer_port);
	}
}

{% if components.printer_settings %}
setInputValue(current_printer_enabled == 1, "printer-virt-yes", "printer-virt-no");
setPrinterPort(current_printerport);
selectListValue("select_printermodel1", current_printer);
{% endif %}

{% if components.modem_settings %}
setInputValue(current_modem_enabled == 1, "modem-virt-yes", "modem-virt-no");
setInputValue(current_modem_sniffer_enabled == 1, "modem-sniffer-yes", "modem-sniffer-no");
{% endif %}

{% if components.hsio_settings %}
selectListValue("select_hsioindex", current_hsioindex);
{% endif %}

{% if components.program_recorder %}
setInputValue(current_cassette_enabled == 1, "pr-virt-yes", "pr-virt-no");
setInputValue(current_play_record == "0 PLAY", "pr-mode-play", "pr-mode-rec");
setInputValue(current_pulldown == "0 B Button Press", "pr-act-b", "pr-act-pull");
{% endif %}

{% if components.disk_swap %}
setInputValue(current_rotation_sounds == 1, "sam-rot-yes", "sam-rot-no");
{% endif %}

{% if components.boot_settings %}
setInputValue(current_boot_mode == 0, "boot-config-mode-config", "boot-config-mode-mount");
setInputValue(current_status_wait_enabled == 1, "boot-sio-wait-yes", "boot-sio-wait-no");
setInputValue(current_config_enabled == 1, "boot-config-disk-yes", "boot-config-disk-no");
setInputValue(current_encrypt_passphrase_enabled == 1, "encrypt-passphrase-yes", "encrypt-passphrase-no");
{% endif %}