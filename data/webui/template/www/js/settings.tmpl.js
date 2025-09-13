function setupExperimentalToggle() {
  const experimentalCheckbox = document.getElementById('experimental');
  const experimentalEnabled = JSON.parse(localStorage.getItem('fujinet.experimental') || 'false');

  if (experimentalEnabled) {
    experimentalCheckbox.checked = true;
    document.body.classList.add('show-experimental');
  }
  experimentalCheckbox.addEventListener('change', toggleExperimental);
}

function toggleExperimental(evt) {
  document.body.classList.toggle('show-experimental');
  localStorage.setItem('fujinet.experimental', evt.target.checked);
}

function setupHostEditing() {
  const editLinks = document.querySelectorAll('a.edit-host');
  editLinks.forEach(link => {
    link.addEventListener('click', e => {
      e.preventDefault();
      const hs = Number(link.dataset.hostslot) - 1;
      const currentHostname = link.dataset.hostname;

      const updatedHostname = prompt(`Enter hostname for slot ${hs + 1}`, currentHostname);

      // Abort on [Cancel]
      if (updatedHostname === null) {
        return;
      }

      fetch(`/hosts?hostslot=${hs}&hostname=${updatedHostname}`, { method: 'POST' })
        .then(() => {
          location.reload();
        })
        .catch(e => {
          alert("Error: Could not update host");
        });
    });
  });
}

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

{% if components.serial_port %}
function setSerialCommand(serial_command) {
	// Serial Command is triple/quadruplevalue (hide 0 for webui).
	const cmd1E = document.getElementById("command-on-dsr");
	const cmd2E = document.getElementById("command-on-cts");
	const cmd3E = document.getElementById("command-on-ri");
	if (cmd1E == null || cmd2E == null || cmd3E == null) return;

	if (serial_command == "1") {
		cmd1E.checked = true;
	} else if (serial_command == "2") {
		cmd2E.checked = true;
	} else if (serial_command == "3") {
		cmd3E.checked = true;
	} else {
		console.log("ERROR: unknown serial_command value:", serial_command);
	}
}

function setSerialProceed(serial_proceed) {
	// Serial Proceed is double/triple value (hide 0 from webui).
	const prc1E = document.getElementById("proceed-on-dtr");
	const prc2E = document.getElementById("proceed-on-rts");
	if (prc1E == null || prc2E == null ) return;

	if (serial_proceed == "1") {
		prc1E.checked = true;
	} else if (serial_proceed == "2") {
		prc2E.checked = true;
	} else {
		console.log("ERROR: unknown serial_proceed value:", serial_proceed);
	}
}
{% endif %}

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
{% if not tweaks.fujinet_pc %}
setInputValue(current_play_record == "0 PLAY", "pr-mode-play", "pr-mode-rec");
{% endif %}
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
setInputValue(current_config_ng == 1, "config-ng-yes", "config-ng-no");
{% endif %}

{% if components.apetime %}
setInputValue(current_apetime == 1, "tz-apetime-yes", "tz-apetime-no");
{% endif %}

{% if components.cpm_settings %}
setInputValue(current_cpm_enabled == 1, "cpm-virt-yes", "cpm-virt-no");
{% endif %}

{% if components.serial_port %}
{% if tweaks.platform == "ATARI" %}
setSerialCommand(current_serial_command);
setSerialProceed(current_serial_proceed);
{% elif tweaks.platform == "COCO" %}
selectListValue("select_serial_baud", current_serial_baud);
{% elif tweaks.platform == "RS232" %}
selectListValue("select_serial_baud", current_serial_baud);
{% endif %}
{% endif %}

{% if components.emulator_settings %}
setInputValue(current_boip_enabled == 1, "boip-yes", "boip-no");
{% endif %}

{% if components.pclink %}
setInputValue(current_pclink == 1, "pclink-yes", "pclink-no");
{% endif %}

setupExperimentalToggle();
setupHostEditing();
