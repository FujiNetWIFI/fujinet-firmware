(function () {
	var data;
	try {
		data = JSON.parse(document.getElementById("macm-data").textContent);
	} catch (e) {
		return;
	}
	window.macmData = data;

	var HD20_MAX_BLOCKS = 65535;

	function el(tag, cls, text) {
		var e = document.createElement(tag);
		if (cls) e.className = cls;
		if (text !== undefined) e.textContent = text;
		return e;
	}
	function size(bytes) {
		if (bytes >= 1048576) return (bytes / 1048576).toFixed(1).replace(/\.0$/, "") + " MB";
		return Math.round(bytes / 1024) + " KB";
	}
	function basename(p) {
		return (p || "").split("/").pop();
	}
	function dirname(p) {
		var i = (p || "").lastIndexOf("/");
		return i > 0 ? p.substring(0, i) : "";
	}
	function chip(text, kind, title) {
		var c = el("span", "macm-chip" + (kind ? " macm-chip-" + kind : ""), text);
		if (title) c.title = title;
		return c;
	}
	function action(text, href, confirmText) {
		var a = el("a", "macm-btn", text);
		a.href = href;
		if (confirmText) {
			a.addEventListener("click", function (e) {
				if (!confirm(confirmText)) e.preventDefault();
			});
		}
		return a;
	}

	var HD_ICON = '<svg viewBox="0 0 32 32" aria-hidden="true"><rect x="2.5" y="10.5" width="27" height="12" fill="#fff" stroke="#000"/><path d="M4.5 20.5h23" stroke="#000"/><rect x="5" y="17" width="3" height="2" fill="#000"/><path d="M2.5 22.5v2h27v-2" fill="none" stroke="#000"/></svg>';
	var FD_ICON = '<svg viewBox="0 0 32 32" aria-hidden="true"><path d="M4.5 3.5h21l3 3v22h-24z" fill="#fff" stroke="#000"/><rect x="9.5" y="3.5" width="12" height="9" fill="#fff" stroke="#000"/><rect x="17" y="5" width="3" height="6" fill="#000"/><rect x="7.5" y="16.5" width="17" height="12" fill="#fff" stroke="#000"/><path d="M10 20h12M10 23h12M10 26h8" stroke="#000"/></svg>';

	function hdChips(s, box) {
		if (s.fs) box.appendChild(chip(s.fs));
		else box.appendChild(chip("No HFS/MFS header", "bad", "Block 2 has no volume signature; the Mac will call this disk damaged."));
		var kinds = { volume: "Volume image", drive: "Drive image → HFS partition", dc42: "DiskCopy 4.2" };
		if (kinds[s.kind]) box.appendChild(chip(kinds[s.kind]));
		box.appendChild(chip(size(s.blocks * 512), "", s.blocks.toLocaleString() + " blocks"));
		if (s.boot === "blessed") box.appendChild(chip("✓ Bootable", "good", "The System Folder is blessed, so the Mac can start up from this volume."));
		else if (s.boot === "blessed-now") box.appendChild(chip("✓ Blessed on mount", "good", "The System Folder wasn't blessed; FujiNet fixed it when this was mounted R/W."));
		else if (s.boot === "unblessed") box.appendChild(chip("System Folder not blessed", "warn", "Mount it R/W once and FujiNet will bless it, or the Mac won't boot from it."));
		else if (s.boot === "no-system") box.appendChild(chip("No System Folder", "", "Fine as a data disk; it can't start the Mac."));
		if (s.truncated) box.appendChild(chip("Truncated", "bad", "The volume claims more blocks than the image file holds; the Mac will report it as damaged."));
		if (s.blocks > HD20_MAX_BLOCKS) box.appendChild(chip("Over 32 MB", "bad", "The Mac rejects HD20 volumes over 65,535 blocks."));
	}
	function fdChips(s, box) {
		box.appendChild(chip(s.sides === 1 ? "400K" : s.sides === 2 ? "800K" : "?"));
		box.appendChild(chip(s.kind === "moof" ? "MOOF flux image" : "Sector image → GCR"));
		if (s.kind === "moof") box.appendChild(chip("Write-protected", "", "MOOF images are always write-protected."));
	}

	function card(s) {
		var hd = s.role === "hd20";
		var title = hd ? "HD20 #" + s.n : "Floppy";
		var state = s.loaded ? "loaded" : s.path ? "pending" : "empty";
		var w = el("div", "macw macw-" + state);
		w.setAttribute("data-slot", s.n);

		var bar = el("div", "macw-title");
		if (s.loaded || s.path) {
			var close = el("a", "macw-close");
			close.href = "/unmount?deviceslot=" + (s.n - 1);
			close.title = "Eject from slot " + s.n;
			close.setAttribute("aria-label", close.title);
			if (s.loaded) {
				close.addEventListener("click", function (e) {
					if (!confirm(ejectWarning(hd, title))) e.preventDefault();
				});
			}
			bar.appendChild(close);
		}
		bar.appendChild(el("span", "", title));
		bar.appendChild(el("em", "macw-slotno", "slot " + s.n));
		w.appendChild(bar);

		var body = el("div", "macw-body");
		var icon = el("div", "macw-icon");
		icon.innerHTML = hd ? HD_ICON : FD_ICON;
		body.appendChild(icon);
		var main = el("div", "macw-main");
		body.appendChild(main);
		w.appendChild(body);

		if (state === "empty") {
			main.appendChild(el("div", "macw-name macw-dim", "Empty"));
			main.appendChild(el("div", "macw-hint", hd
				? (s.n === 1 ? "Startup volume goes here." : "Room for another hard disk.")
				: "No disk in the drive."));
			return w;
		}

		var inner = s.sit ? s.sit.inner : "";
		var name = (s.loaded && s.vol) ? s.vol : (inner || basename(s.path));
		main.appendChild(el("div", "macw-name", name));
		var file = el("div", "macw-file");
		file.textContent = s.host + " :: " + s.path + (inner ? " → " + inner : "");
		file.title = file.textContent;
		main.appendChild(file);

		var chips = el("div", "macw-chips");
		main.appendChild(chips);

		if (state === "pending") {
			w.appendChild(el("div", "macw-banner", "In the config, but not on the Mac yet — press Mount All."));
			chips.appendChild(chip(s.rw ? "R/W" : "Read-only"));
			return w;
		}

		chips.appendChild(s.ro ? chip("🔒 Read-only", "", "Mounted read-only: the Mac sees a locked disk.")
			: chip("R/W", "", "Mounted read/write: the Mac can write to this image."));
		if (hd) hdChips(s, chips); else fdChips(s, chips);
		if (s.sit) {
			chips.appendChild(chip("📦 " + s.sit.format + (s.sit.ndif ? " · NDIF" : ""), "arc",
				"Unpacked from " + basename(s.path) + " (" + s.sit.method + ") into " + size(s.sit.bytes) + " of PSRAM. Writes stay in PSRAM and are lost on eject."));
		}

		if (hd) {
			var pct = Math.min(100, 100 * s.blocks / HD20_MAX_BLOCKS);
			var meter = el("div", "macw-meter" + (s.blocks > HD20_MAX_BLOCKS ? " macw-meter-over" : ""));
			var fill = el("i");
			fill.style.width = pct.toFixed(1) + "%";
			meter.appendChild(fill);
			meter.title = s.blocks.toLocaleString() + " of " + HD20_MAX_BLOCKS.toLocaleString() + " blocks (the HD20 limit)";
			main.appendChild(meter);
		}

		var acts = el("div", "macw-actions");
		acts.appendChild(action("Eject", "/unmount?deviceslot=" + (s.n - 1), ejectWarning(hd, title)));
		acts.appendChild(action("Swap from this folder…", "/hsdir?hostslot=" + s.hs + "&path=" + encodeURIComponent(dirname(s.path))));
		if (s.sit) acts.appendChild(action("Download image", "/sitdownload?deviceslot=" + (s.n - 1)));
		w.appendChild(acts);
		return w;
	}

	function ejectWarning(hd, title) {
		return hd
			? "Eject " + title + "?\n\nShut the Mac down or drag this volume to the Trash first. Otherwise the Mac writes its cached volume header into the next image mounted here."
			: "Eject the floppy?\n\nIt's better to drag the disk to the Trash on the Mac first, so both sides agree it's gone.";
	}

	var unused = document.getElementById("macm-unused");
	var unusedText = unused.textContent;

	// Draws the cards, hosts and PSRAM meter; rerun by the activity view on mount changes
	function render(data) {
		var hdBox = document.getElementById("macm-hd");
		var fdBox = document.getElementById("macm-fd");
		var hosts = document.getElementById("macm-hosts");
		var p = document.getElementById("macm-psram");
		hdBox.innerHTML = fdBox.innerHTML = hosts.innerHTML = p.innerHTML = "";
		unused.classList.remove("macm-unused-warn");
		unused.textContent = unusedText;

		var stray = [];
		data.slots.forEach(function (s) {
			if (s.role === "hd20") hdBox.appendChild(card(s));
			else if (s.role === "floppy") fdBox.appendChild(card(s));
			else if (s.path) stray.push(s.n);
		});
		if (stray.length) {
			unused.classList.add("macm-unused-warn");
			unused.textContent = "Slot" + (stray.length > 1 ? "s " : " ") + stray.join(", ") +
				" has an image in the config, but slots 6–8 aren't connected to the Mac. Mount it in slots 1–5 instead.";
		}

		if (data.hosts.length) {
			hosts.appendChild(el("span", "macm-hosts-label", "Find an image on"));
			data.hosts.forEach(function (h) {
				if (!h.name) return;
				var a = el("a", "macm-host", h.name);
				a.href = "/hsdir?hostslot=" + h.hs;
				hosts.appendChild(a);
			});
		} else {
			hosts.appendChild(el("span", "macm-hosts-label", "No hosts yet. Add one in the Hosts List to browse for images."));
		}

		if (data.psram_total) {
			var used = data.psram_total - data.psram_free;
			p.appendChild(el("span", "macm-psram-label", "PSRAM"));
			var m = el("div", "macw-meter");
			var f = el("i");
			f.style.width = (100 * used / data.psram_total).toFixed(1) + "%";
			m.appendChild(f);
			p.appendChild(m);
			p.appendChild(el("span", "macm-psram-free", size(data.psram_free) + " free"));
			p.title = "Floppies (about 1.2 MB of GCR tracks for 800K) and archive images live in PSRAM.";
		}
	}

	render(data);
	window.macmRender = render;
})();

// Live activity: polls /mac/activity and animates each counter change as packets on the wires
(function () {
	var data = window.macmData;
	var box = document.getElementById("macm-wire");
	if (!data || !box || !window.fetch) return;

	var NS = "http://www.w3.org/2000/svg";
	var POLL_MS = 500;
	var MAX_PACKETS = 90;
	var SPEED = 0.42; // px per ms

	function svg(tag, attrs, parent) {
		var e = document.createElementNS(NS, tag);
		for (var k in attrs) e.setAttribute(k, attrs[k]);
		if (parent) parent.appendChild(e);
		return e;
	}
	function text(parent, x, y, str, cls, anchor) {
		var t = svg("text", { x: x, y: y, "class": cls || "", "text-anchor": anchor || "middle" }, parent);
		t.textContent = str;
		return t;
	}
	function shorten(str, n) {
		return str.length > n ? str.substring(0, n - 1) + "…" : str;
	}
	function rate(bytesPerSec) {
		if (bytesPerSec >= 1048576) return (bytesPerSec / 1048576).toFixed(1) + " MB/s";
		if (bytesPerSec >= 1024) return Math.round(bytesPerSec / 1024) + " KB/s";
		return Math.round(bytesPerSec) + " B/s";
	}

	var slots = data.slots.filter(function (s) { return s.role !== "none"; });
	var root, pktLayer, W, H = 200, BUS_Y = 62, MAC_X = 22, FUJI_X, nodes = {};

	function layout() {
		W = Math.max(box.clientWidth, 600);
		box.innerHTML = "";
		root = svg("svg", { viewBox: "0 0 " + W + " " + H, width: "100%", "class": "macm-wire-svg" }, box);
		nodes = {};

		// the Mac: a compact Mac with a Happy Mac on its screen
		var mac = svg("g", { transform: "translate(" + MAC_X + ",14)", "class": "wire-mac" }, root);
		svg("rect", { x: 0.5, y: 0.5, width: 66, height: 86, rx: 6, "class": "wire-case" }, mac);
		svg("rect", { x: 9.5, y: 8.5, width: 48, height: 38, rx: 3, "class": "wire-screen" }, mac);
		svg("path", { d: "M26 20v5M41 20v5M33 22v8h-3M26 35q7 6 15 0", "class": "wire-face" }, mac);
		svg("path", { d: "M36 64h20", "class": "wire-line" }, mac);
		nodes.macLed = svg("rect", { x: 9, y: 71, width: 10, height: 5, rx: 1, "class": "wire-led" }, mac);
		svg("path", { d: "M8 86.5v6h51v-6", "class": "wire-line" }, mac);
		text(root, MAC_X + 33, 124, "Macintosh", "wire-label wire-strong");
		text(root, MAC_X + 33, 138, "floppy port", "wire-label");

		// cable from the port on the back of the Mac to FujiNet
		FUJI_X = MAC_X + 190;
		svg("path", { d: "M" + (MAC_X + 67) + " " + BUS_Y + "H" + (FUJI_X - 40), "class": "wire-cable" }, root);
		svg("rect", { x: MAC_X + 67, y: BUS_Y - 5, width: 8, height: 10, "class": "wire-plug" }, root);
		text(root, MAC_X + 118, BUS_Y - 9, "DB-19", "wire-label wire-small");

		// packets run on the wires but behind FujiNet and the drives
		pktLayer = svg("g", {}, root);

		// FujiNet
		var fuji = svg("g", { transform: "translate(" + (FUJI_X - 40) + "," + (BUS_Y - 17) + ")" }, root);
		svg("rect", { x: 0.5, y: 0.5, width: 80, height: 34, rx: 4, "class": "wire-fuji" }, fuji);
		text(fuji, 36, 22, "FujiNet", "wire-fuji-text");
		nodes.fujiLed = svg("circle", { cx: 70, cy: 10, r: 4.5, "class": "wire-led" }, fuji);

		// the bus and one tap per slot
		var first = FUJI_X + 90, last = W - 55;
		var step = (last - first) / (slots.length - 1);
		root.insertBefore(svg("path", { d: "M" + (FUJI_X + 41) + " " + BUS_Y + "H" + last, "class": "wire-bus" }), pktLayer);

		slots.forEach(function (s, i) {
			var x = Math.round(first + i * step);
			var hd = s.role === "hd20";
			var loaded = !!s.loaded;
			var g = svg("g", { "class": "wire-dev" + (loaded ? "" : " wire-off") }, root);
			svg("circle", { cx: x, cy: BUS_Y, r: 3, "class": "wire-node" }, g);
			svg("path", { d: "M" + x + " " + BUS_Y + "V" + (BUS_Y + 38), "class": "wire-tap" }, g);
			var n = { x: x, g: g, loaded: loaded, last: null, slot: s };
			var iy = BUS_Y + 38;
			if (hd) {
				svg("rect", { x: x - 26.5, y: iy + 0.5, width: 53, height: 24, rx: 2, "class": "wire-disk" }, g);
				svg("path", { d: "M" + (x - 22) + " " + (iy + 18.5) + "H" + (x + 22), "class": "wire-line" }, g);
				n.led = svg("rect", { x: x - 22, y: iy + 10, width: 11, height: 6, rx: 1, "class": "wire-led" }, g);
			} else {
				svg("path", { d: "M" + (x - 16.5) + " " + (iy + 0.5) + "h30l3 3v31h-33z", "class": "wire-disk" }, g);
				svg("rect", { x: x - 9.5, y: iy + 0.5, width: 17, height: 11, "class": "wire-disk" }, g);
				svg("rect", { x: x + 1, y: iy + 2, width: 4, height: 8, "class": "wire-fill" }, g);
				n.hub = svg("g", { "class": "wire-hub", style: "transform-origin:" + x + "px " + (iy + 23) + "px" }, g);
				svg("circle", { cx: x, cy: iy + 23, r: 6.5, "class": "wire-disk" }, n.hub);
				svg("rect", { x: x - 1.5, y: iy + 17, width: 3, height: 5, "class": "wire-fill" }, n.hub);
				n.led = svg("circle", { cx: x - 11, cy: iy + 30, r: 3.2, "class": "wire-led" }, g);
			}
			var name = loaded ? (s.vol || (s.sit && s.sit.inner) || (s.path || "").split("/").pop()) : "empty";
			text(g, x, iy + 52, hd ? "HD20 #" + s.n : "Floppy", "wire-label wire-strong");
			var nm = text(g, x, iy + 66, shorten(name, Math.max(8, Math.floor(step / 7))), "wire-label");
			nm.appendChild(svg("title", {})).textContent = name;
			n.stat = text(g, x, iy + 80, loaded ? "idle" : "", "wire-label wire-small wire-stat");
			nodes[s.n] = n;
		});
	}

	// ---- packets: two hops, drive -> FujiNet -> Mac (writes the other way)
	var packets = [], running = false;
	var HEX = "0123456789ABCDEF";
	var HOLD_MS = 160;

	function legLen(pts) {
		var len = 0;
		for (var i = 1; i < pts.length; i++) len += Math.abs(pts[i][0] - pts[i - 1][0]) + Math.abs(pts[i][1] - pts[i - 1][1]);
		return len;
	}
	function route(n, toMac) {
		var drive = [[n.x, BUS_Y + 36], [n.x, BUS_Y], [FUJI_X + 41, BUS_Y]]; // drive -> FujiNet
		var mac = [[FUJI_X - 40, BUS_Y], [MAC_X + 72, BUS_Y]];                 // FujiNet -> Mac
		return toMac ? [drive, mac] : [mac.slice().reverse(), drive.slice().reverse()];
	}
	// how long a trip takes, so a reply can be timed after its request
	function tripMs(n) {
		var legs = route(n, true);
		return (legLen(legs[0]) + legLen(legs[1])) / SPEED + HOLD_MS;
	}

	function spawn(n, toMac, delay, kind) {
		if (packets.length >= MAX_PACKETS) return;
		var g = svg("g", { "class": "wire-pkt wire-pkt-" + kind, opacity: 0 }, pktLayer);
		if (kind === "q" || kind === "s") {
			// request / status ping: a small dot, no payload
			svg("circle", { r: kind === "q" ? 4 : 3 }, g);
		} else {
			svg("rect", { x: -10, y: -7, width: 20, height: 14, rx: 7 }, g);
			var t = svg("text", { x: 0, y: 3.5, "text-anchor": "middle" }, g);
			t.textContent = HEX[Math.random() * 16 | 0] + HEX[Math.random() * 16 | 0];
		}
		var legs = route(n, toMac);
		packets.push({ g: g, n: n, toMac: toMac, kind: kind, legs: legs, leg: 0,
			len: legLen(legs[0]), t0: performance.now() + delay });
		if (!running) { running = true; requestAnimationFrame(frame); }
	}

	function at(pts, d) {
		for (var i = 1; i < pts.length; i++) {
			var a = pts[i - 1], b = pts[i];
			var seg = Math.abs(b[0] - a[0]) + Math.abs(b[1] - a[1]);
			if (d <= seg) {
				var f = seg ? d / seg : 0;
				return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f];
			}
			d -= seg;
		}
		return pts[pts.length - 1];
	}

	function ledKind(kind) {
		return kind === "w" ? "w" : kind === "e" ? "e" : "r";
	}

	function frame(now) {
		packets = packets.filter(function (p) {
			var d = (now - p.t0) * SPEED;
			if (d < 0) { p.g.setAttribute("opacity", 0); return true; }
			if (d >= p.len) {
				if (p.leg === 0) {
					// into FujiNet: hand off, then out the other side
					blink(nodes.fujiLed, ledKind(p.kind));
					p.leg = 1;
					p.len = legLen(p.legs[1]);
					p.t0 = now + HOLD_MS;
					p.g.setAttribute("opacity", 0);
					return true;
				}
				// arrived: light the receiving end
				blink(p.toMac ? nodes.macLed : p.n.led, ledKind(p.kind));
				p.g.remove();
				return false;
			}
			var xy = at(p.legs[p.leg], d);
			p.g.setAttribute("transform", "translate(" + xy[0].toFixed(1) + "," + xy[1].toFixed(1) + ")");
			// fade in and out at the ends of each hop
			p.g.setAttribute("opacity", Math.min(1, d / 18, (p.len - d) / 18).toFixed(2));
			return true;
		});
		if (packets.length) requestAnimationFrame(frame);
		else running = false;
	}

	function blink(el, kind) {
		if (!el) return;
		el.classList.remove("wire-led-r", "wire-led-w", "wire-led-e");
		el.getBoundingClientRect(); // restart the animation
		el.classList.add("wire-led-" + kind);
	}

	// ---- polling
	var prev = null, timer = null, failures = 0;

	function burst(n, count, toMac, kind, dt, after) {
		// one packet per block for a trickle, capped for a flood
		var shown = Math.min(count, 8);
		for (var i = 0; i < shown; i++) spawn(n, toMac, (after || 0) + (i * dt) / shown, kind);
	}

	function update(a) {
		var changed = [];
		a.slots.forEach(function (s) {
			var n = nodes[s.n];
			if (!n) return;
			if (s.loaded !== n.loaded) {
				// changed behind the page's back, e.g. the Mac ejected its floppy
				n.loaded = s.loaded;
				n.g.classList.toggle("wire-off", !s.loaded);
				n.stat.textContent = s.loaded ? "loading…" : "ejected";
				changed.push(s.n);
			}
			var p = prev && prev.slots[s.n - 1];
			var dt = prev ? Math.max(1, a.ms - prev.ms) : POLL_MS;
			var dr = p ? s.r - p.r : 0, dw = p ? s.w - p.w : 0, de = p ? s.e - p.e : 0;
			if (dr < 0 || dw < 0) { dr = 0; dw = 0; } // counters restarted with the ESP32
			var hd = n.slot.role === "hd20";

			var ds = p ? (s.s || 0) - (p.s || 0) : 0;
			var trip = tripMs(n);
			// read: the Mac asks (a dot runs out), the blocks come back
			if (dr > 0) { spawn(n, false, 0, "q"); burst(n, dr, true, "r", dt, trip); }
			// write: the blocks run out, an acknowledgement comes back
			if (dw > 0) { burst(n, dw, false, "w", dt); spawn(n, true, trip + dt, "q"); }
			if (de > 0) spawn(n, true, 0, "e");
			// status poll: a ping out and a small reply, only when otherwise quiet
			if (ds > 0 && !dr && !dw) { spawn(n, false, 0, "s"); spawn(n, true, trip, "s"); }

			if (!hd) {
				if (n.hub) n.hub.classList.toggle("wire-spin", !!s.spin);
				// a spinning drive streams its track to the Mac continuously
				if (s.spin && !dr) spawn(n, true, Math.random() * POLL_MS, "r");
			}

			if (!s.loaded || !p) return;
			if (hd) {
				var parts = [];
				if (dr) parts.push("▲ " + rate(dr * 512 * 1000 / dt));
				if (dw) parts.push("▼ " + rate(dw * 512 * 1000 / dt));
				if (de) parts.push("⚠ error");
				n.stat.textContent = parts.length ? parts.join("  ") : "idle";
			} else {
				n.stat.textContent = s.spin ? "spinning · cyl " + s.cyl + (dw ? " · writing" : "") : "motor off";
			}
			n.stat.classList.toggle("wire-busy", !!(dr || dw || s.spin));
		});
		prev = a;
		if (changed.length) refresh(changed, 0);
	}

	// Refetch the slots and redraw; waits because the firmware clears the config after the eject ack
	var refreshing = false;
	function refresh(changed, attempt) {
		if (refreshing) return;
		refreshing = true;
		setTimeout(function () {
			fetch("/mac/slots", { cache: "no-store" })
				.then(function (r) { return r.json(); })
				.then(function (d) {
					refreshing = false;
					var settling = changed.some(function (n) {
						var s = d.slots[n - 1];
						return s && !s.loaded && s.path; // config not cleared yet
					});
					if (settling && attempt < 3) { refresh(changed, attempt + 1); return; }
					data = window.macmData = d;
					window.macmRender(d);
					slots = d.slots.filter(function (s) { return s.role !== "none"; });
					packets.forEach(function (p) { p.g.remove(); });
					packets = [];
					layout();
					changed.forEach(function (n) {
						var c = document.querySelector('.macw[data-slot="' + n + '"]');
						if (c) c.classList.add("macw-flash");
					});
				})
				.catch(function () { refreshing = false; });
		}, 600 * (attempt + 1));
	}

	function poll() {
		timer = null;
		if (document.hidden) return; // resumes on visibilitychange
		var ctl = window.AbortController ? new AbortController() : null;
		var kill = ctl ? setTimeout(function () { ctl.abort(); }, 3000) : null;
		fetch("/mac/activity", { cache: "no-store", signal: ctl ? ctl.signal : undefined })
			.then(function (r) {
				if (r.status === 401) throw "auth";
				return r.json();
			})
			.then(function (a) {
				failures = 0;
				box.classList.remove("wire-offline");
				update(a);
				timer = setTimeout(poll, POLL_MS);
			})
			.catch(function (e) {
				if (e === "auth") return; // logged out: stop quietly
				failures++;
				box.classList.add("wire-offline");
				prev = null;
				timer = setTimeout(poll, Math.min(10000, 1000 * failures));
			})
			.then(function () { if (kill) clearTimeout(kill); });
	}

	document.addEventListener("visibilitychange", function () {
		if (!document.hidden && !timer) { prev = null; poll(); }
	});
	var lastW = 0;
	window.addEventListener("resize", function () {
		if (Math.abs(box.clientWidth - lastW) < 40) return;
		lastW = box.clientWidth;
		packets.forEach(function (p) { p.g.remove(); });
		packets = [];
		layout();
	});

	lastW = box.clientWidth;
	layout();
	poll();
})();
