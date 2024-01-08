function writeLocaleNumber(num, id) {
	const e = document.getElementById(id);
	if (e !== null) {
		e.innerText = num.toLocaleString();
	}
}

function writeUptimeString(secs, id) {

	var mins = secs / 60 | 0;
	var hours = mins / 60 | 0;
	var days = hours / 24 | 0;

	var result = '';

	if (days)
		result += days + ' days, ';
	if (hours % 24)
		result += (hours % 24) + ' hours, ';
	if (mins % 60)
		result += (mins % 60) + ' minutes, ';
	if (secs % 60)
		result += (secs % 60) + ' seconds';
	else if (!secs)
		result = '0 seconds';

	const e = document.getElementById(id);
	if (e !== null) {
		e.innerText = result;
	}
}

{% if tweaks.fujinet_pc %}
function restartButton() {
	var btn = document.getElementById("restartButton");
	if (btn.value == "Confirm")
		window.location.assign("/restart");
	if (btn.value == "Restart...")
		btn.value = "Confirm";
		setTimeout(function(){
			btn.value = "Restart...";
		}, 2000);
}

function swapButton() {
	window.location.assign("/swap?redirect=1");
}

function mountAllButton() {
	window.location.assign("/mount?mountall=1&redirect=1");
}
{% endif %}

