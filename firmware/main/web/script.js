/* openextraflame - Web UI script */
const $ = id => document.getElementById(id);
const $$ = sel => document.querySelectorAll(sel);
const j = (u, o = {}) => fetch(u, o).then(r => r.json());

/* --- Toast notifications (=replace native alert()) --- */
/* Non-blocking notifications shown top-right, auto-dismiss after 4 s.
 * Types : 'info' (bleu), 'success' (vert), 'warn' (orange), 'error' (rouge). */
function toast(msg, type = 'info', ms = 4000) {
    const c = $('toast-container');
    if (!c) return;  /* safe when DOM not ready */
    const el = document.createElement('div');
    el.className = `toast toast-${type}`;
    el.textContent = msg;
    c.appendChild(el);
    /* Slide in on next frame */
    requestAnimationFrame(() => el.classList.add('show'));
    setTimeout(() => {
        el.classList.remove('show');
        setTimeout(() => el.remove(), 300);
    }, ms);
}

/* Modal confirm (=replace native confirm() which is silently blocked on
 * some mobile Chrome contexts). Returns a Promise<boolean>. */
function ask(msg) {
    return new Promise(resolve => {
        $('confirm-msg').textContent = msg;
        $('confirm-backdrop').hidden = false;
        const done = ok => {
            $('confirm-backdrop').hidden = true;
            $('confirm-ok').onclick = null;
            $('confirm-cancel').onclick = null;
            resolve(ok);
        };
        $('confirm-ok').onclick = () => done(true);
        $('confirm-cancel').onclick = () => done(false);
    });
}

/* Theme toggle */
function initTheme() {
    const saved = localStorage.getItem('theme');
    if (saved) document.documentElement.setAttribute('data-theme', saved);
    $('theme-btn').addEventListener('click', () => {
        const cur = document.documentElement.getAttribute('data-theme');
        const next = cur === 'dark' ? 'light' : cur === 'light' ? 'auto' : 'dark';
        if (next === 'auto') document.documentElement.removeAttribute('data-theme');
        else document.documentElement.setAttribute('data-theme', next);
        localStorage.setItem('theme', next);
    });
}

/* Tabs */
function initTabs() {
    $$('.tab').forEach(t => {
        t.addEventListener('click', () => {
            $$('.tab').forEach(x => x.classList.remove('active'));
            $$('.tab-content').forEach(x => x.classList.remove('active'));
            t.classList.add('active');
            $('tab-' + t.dataset.tab).classList.add('active');
        });
    });
}

/* Load status periodically */
async function loadStatus() {
    try {
        const s = await j('/status.json');
        const wifiOk = !!s.wifi?.connected;
        const stoveOk = !!s.stove?.online;

        $('s-wifi').textContent = wifiOk ? s.wifi.ssid : 'Déconnecté';
        $('s-ip').textContent = s.wifi?.ip || '-';
        $('s-mqtt').textContent = s.mqtt?.connected ? 'Connecté' : 'Déconnecté';
        $('s-stove').textContent = stoveOk ? 'En ligne' : 'Hors ligne';

        $('m-tamb').textContent = s.stove?.t_ambient?.toFixed?.(1) ?? '-';
        const p = s.stove?.power ?? null;
        $('m-power').textContent = p == null ? '-' : String(p);
        const pr = s.stove?.power_real ?? null;
        if ($('m-power-real')) $('m-power-real').textContent = pr == null ? '-' : String(pr);
        $('m-fumi').textContent = s.stove?.t_smoke?.toFixed?.(0) ?? '-';
        /* Enum Extraflame TotalControl (0x21 machineState) */
        const STATES = ['Off','Check up','Ignition','Préparation','Préchargement','Modulation','En marche','Nettoyage','Refroidissement','Veille','Nettoyage final','Recovery','Allumage final'];
        if ($('m-state')) {
            const st = s.stove?.state ?? null;
            $('m-state').textContent = st == null ? '-' : (STATES[st] || 'état ' + st);
        }

        const dot = $('status-dot');
        dot.className = 'status-dot';
        if (wifiOk && stoveOk) dot.classList.add('ok');
        else if (!wifiOk) dot.classList.add('err');

    } catch (e) {
        console.warn('status error', e);
    }
}

/* Load config into forms */
async function loadConfig() {
    try {
        const c = await j('/config.json');
        $('wifi-ssid').value = c.wifi_ssid || '';
        $('mqtt-host').value = c.mqtt_host || '';
        $('mqtt-port').value = c.mqtt_port || 1883;
        $('mqtt-user').value = c.mqtt_user || '';
        $('mqtt-prefix').value = c.mqtt_prefix || 'extraflame';
        $('mqtt-tls').checked = !!c.mqtt_tls;
        $('stove-name').value = c.stove_name || 'poele';
        $('stove-type').value = c.stove_type || 0;
        $('ha-discovery').checked = !!c.ha_discovery;
        $('publish-interval').value = c.publish_interval_ms || 5000;

        /* Password fields: never echo the stored value. Instead, use the
         * placeholder to signal explicitly whether it's already set. If it
         * is, the user knows to LEAVE THE FIELD BLANK to keep the current
         * password. If it's not set, they must fill it. */
        const wifiPwd = $('wifi-pwd');
        const mqttPwd = $('mqtt-pwd');
        wifiPwd.value = '';
        mqttPwd.value = '';
        if (c.wifi_password_set) {
            wifiPwd.placeholder = '•••••••• (défini, laisser vide pour ne pas changer)';
            wifiPwd.dataset.stored = '1';
        } else {
            wifiPwd.placeholder = 'Mot de passe Wi-Fi';
            wifiPwd.dataset.stored = '';
        }
        if (c.mqtt_password_set) {
            mqttPwd.placeholder = '•••••••• (défini, laisser vide pour ne pas changer)';
            mqttPwd.dataset.stored = '1';
        } else {
            mqttPwd.placeholder = 'Mot de passe MQTT (optionnel)';
            mqttPwd.dataset.stored = '';
        }
        /* Same trick for mqtt_user: shows the user whether a user is
         * already stored (=leave blank to keep it) or if the field is
         * genuinely empty and needs filling. */
        const mqttUser = $('mqtt-user');
        mqttUser.value = c.mqtt_user || '';
        if (c.mqtt_user_set) {
            mqttUser.placeholder = 'Utilisateur défini (laisser vide pour ne pas changer)';
        } else {
            mqttUser.placeholder = 'Utilisateur (=souvent le user HA)';
        }

        /* Guardian mode - only visible on Target Blacklabel */
        if (c.guardian_supported === false) {
            $('guardian-card').style.display = 'none';
        } else {
            $('guardian-enabled').checked = !!c.guardian_enabled;
            $('guardian-url').value = c.guardian_url || '';
            $('guardian-action').value = c.guardian_action || 0;
            toggleGuardianSettings();
        }

        const v = c.version || 'v?';
        $('ota-current-version').textContent = v;
        $('version').textContent = v;   /* header tagline */
    } catch (e) {
        console.warn('config error', e);
    }
}

function toggleGuardianSettings() {
    const on = $('guardian-enabled').checked;
    $('guardian-settings').style.display = on ? 'block' : 'none';
}

/* Wi-Fi scan */
async function scan() {
    const sel = $('ssid-select');
    sel.innerHTML = '<option>Scan en cours...</option>';
    try {
        const aps = await j('/ap.json');
        sel.innerHTML = '<option value="">-- Choisir --</option>';
        aps.sort((a, b) => b.rssi - a.rssi);
        aps.forEach(ap => {
            const o = document.createElement('option');
            o.value = ap.ssid;
            const lock = ap.auth > 0 ? '🔒' : '';
            o.textContent = `${lock} ${ap.ssid} (${ap.rssi} dBm)`;
            sel.appendChild(o);
        });
    } catch (e) {
        sel.innerHTML = '<option>Erreur scan</option>';
    }
}

/* Save config */
async function save() {
    const cfg = {
        wifi_ssid: $('wifi-ssid').value,
        wifi_pwd: $('wifi-pwd').value,
        mqtt_host: $('mqtt-host').value,
        mqtt_port: parseInt($('mqtt-port').value, 10),
        mqtt_user: $('mqtt-user').value,
        mqtt_pwd: $('mqtt-pwd').value,
        mqtt_prefix: $('mqtt-prefix').value,
        mqtt_tls: $('mqtt-tls').checked,
        stove_name: $('stove-name').value,
        stove_type: parseInt($('stove-type').value, 10),
        ha_discovery: $('ha-discovery').checked,
        publish_interval_ms: parseInt($('publish-interval').value, 10),
    };
    if ($('guardian-card').style.display !== 'none') {
        cfg.guardian_enabled = $('guardian-enabled').checked;
        cfg.guardian_url = $('guardian-url').value;
        cfg.guardian_token = $('guardian-token').value;
        cfg.guardian_action = parseInt($('guardian-action').value, 10);
    }
    const save_btn = $('save');
    const orig_label = save_btn.textContent;
    save_btn.disabled = true;
    save_btn.textContent = '⏳ Sauvegarde...';
    try {
        const r = await fetch('/config.json', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(cfg),
        });
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        toast('✅ Configuration sauvegardée. Redémarrage...', 'success');
        setTimeout(async () => {
            try { await fetch('/reboot', {method: 'POST'}); } catch (e) {}
            setTimeout(() => location.reload(), 8000);
        }, 800);
    } catch (e) {
        toast(`❌ Échec sauvegarde : ${e.message}`, 'error', 6000);
        save_btn.disabled = false;
        save_btn.textContent = orig_label;
    }
}

/* Factory reset */
async function factory() {
    const ok = await ask('⚠️ Effacer toute la configuration et redémarrer ? Le module reviendra en mode SoftAP de provisioning.');
    if (!ok) return;
    const btn = $('factory');
    btn.textContent = '⏳ Reset usine...';
    btn.disabled = true;
    try { await fetch('/factory', {method: 'POST'}); } catch (e) {}
    setTimeout(() => location.reload(), 10000);
}

/* Reboot */
async function reboot() {
    const btn = $('reboot');
    btn.textContent = '⏳ Redémarrage en cours...';
    btn.disabled = true;
    try { await fetch('/reboot', {method: 'POST'}); } catch (e) { /* socket dies mid-response, expected */ }
    setTimeout(() => location.reload(), 10000);
}

/* Stove commands */
async function stoveCmd(cmd) {
    try {
        const r = await fetch('/api/stove/' + cmd, {method: 'POST'});
        if (!r.ok) {
            toast(`❌ Commande poêle refusée (HTTP ${r.status})`, 'error');
            return;
        }
        toast(`✅ Commande '${cmd}' envoyée`, 'success', 2500);
    } catch (e) {
        toast(`❌ Impossible d'envoyer la commande : ${e.message}`, 'error');
    }
}

/* OTA upload direct (=browser -> module -> flash) */
async function otaUpload(file) {
    const progress = $('ota-progress');
    const bar = $('ota-bar');
    const text = $('ota-text');
    progress.style.display = 'block';
    toast('⚠️ Ne coupe surtout PAS l\'alim du module pendant le flash', 'warn', 8000);
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/ota/upload');
    xhr.upload.addEventListener('progress', e => {
        if (e.lengthComputable) {
            const pct = (e.loaded / e.total * 100).toFixed(0);
            bar.style.width = pct + '%';
            text.textContent = `📥 Envoi vers module : ${pct}%`;
        }
    });
    xhr.onload = () => {
        if (xhr.status === 200) {
            bar.style.width = '100%';
            text.textContent = '🔒 Vérification + application + reboot... NE PAS COUPER L\'ALIM';
            setTimeout(() => location.reload(), 10000);
        } else {
            text.textContent = `❌ Erreur HTTP ${xhr.status}`;
            toast(`Upload rejeté : HTTP ${xhr.status}`, 'error', 6000);
        }
    };
    xhr.onerror = () => {
        text.textContent = '❌ Connexion perdue pendant l\'upload';
        toast('Connexion perdue pendant l\'upload - retente', 'error', 6000);
    };
    xhr.send(file);
}

async function otaRollback() {
    const ok = await ask('Retour à la version précédente ? Le module va rebooter sur le slot OTA d\'avant.');
    if (!ok) return;
    try {
        await fetch('/ota/rollback', {method: 'POST'});
        toast('↩️ Rollback en cours, reboot dans quelques secondes', 'info', 5000);
    } catch (e) { /* connection dies mid-response, expected */ }
    setTimeout(() => location.reload(), 5000);
}

async function otaCheck() {
    const span = $('ota-latest');
    span.textContent = '⏳ Interrogation GitHub...';
    try {
        const r = await fetch('https://api.github.com/repos/Shad107/OpenXtraflame/releases/latest');
        if (!r.ok) {
            span.textContent = r.status === 404
                ? '❌ Repo privé ou pas de release publique'
                : `❌ HTTP ${r.status}`;
            return;
        }
        const j = await r.json();
        const latest = j.tag_name || j.name || '?';
        const current = $('version').textContent;
        const url = (j.assets || []).map(a => a.browser_download_url).find(u => /openextraflame\.bin$/.test(u));
        if (url) $('ota-url').value = url;
        span.textContent = current === latest
            ? `✅ ${current} = latest`
            : `⚠️ Installé ${current}, dispo ${latest}`;
    } catch (e) {
        span.textContent = '❌ Erreur réseau';
    }
}

/* OTA state enum from firmware ota.h - kept aligned by convention. */
const OTA_STATE_LABEL = {
    0: 'Prêt',
    1: '📥 Téléchargement du firmware...',
    2: '🔒 Vérification signature SHA256...',
    3: '🔄 Application + reboot dans quelques secondes',
    4: '❌ Échec OTA',
};

async function otaPull() {
    const url = $('ota-url').value.trim();
    if (!url) { toast('Renseigne une URL avant de cliquer Pull', 'warn'); return; }
    const btn = $('ota-pull');
    const progress = $('ota-progress');
    const bar = $('ota-bar');
    const text = $('ota-text');
    btn.textContent = '⏳ En cours...';
    btn.disabled = true;
    progress.style.display = 'block';
    bar.style.width = '2%';
    text.textContent = '📥 Démarrage...';
    toast('⚠️ Ne coupe surtout PAS l\'alim du module pendant le flash', 'warn', 8000);

    /* Fire and forget: /ota/pull blocks until reboot. We poll /ota/status
     * in parallel to reflect the real state to the user. */
    fetch('/ota/pull', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({url}),
    }).catch(() => {});

    let done = false;
    let last_written = 0;
    let consecutive_fetch_fails = 0;
    const start = performance.now();

    const finish_failed = (msg) => {
        if (done) return;
        done = true;
        clearInterval(poll);
        toast(`❌ Échec OTA : ${msg || 'raison inconnue'}`, 'error', 8000);
        btn.textContent = '⬇️ Pull';
        btn.disabled = false;
        progress.style.display = 'none';
        text.textContent = '';
        bar.style.width = '0%';
    };

    /* Hard timeout so the UI never stays locked forever: if after 90 s
     * we haven't seen state=3 or state=4, we surface a generic failure
     * and unlock. UNLESS we've seen bytes go through - in that case
     * the module has already committed to the OTA and is rebooting,
     * showing '❌ Échec' would be a lie. Fired via finish_failed which
     * itself is a no-op once done=true. */
    const hard_timeout = setTimeout(() => {
        if (last_written > 0) {
            /* Rebooted successfully; just reload. */
            if (done) return;
            done = true;
            clearInterval(poll);
            text.textContent = '🔄 Module en reboot, page rechargée bientôt';
            setTimeout(() => location.reload(), 5000);
        } else {
            finish_failed('Timeout : pas de progression après 90 s. Vérifie l\'URL et la connectivité.');
        }
    }, 90000);

    const poll = setInterval(async () => {
        try {
            const s = await fetch('/ota/status').then(r => r.json());
            const label = OTA_STATE_LABEL[s.state] || `État ${s.state}`;
            const pct = s.total > 0
                ? Math.min(99, Math.floor(100 * s.written / s.total))
                : 5;
            bar.style.width = pct + '%';
            text.textContent = `${label} (${pct}%)`;
            last_written = s.written || last_written;
            consecutive_fetch_fails = 0;  /* reset streak on any success */
            if (s.state === 3) {          /* REBOOTING */
                text.textContent = '🔄 Reboot en cours, patiente ~10 s...';
                bar.style.width = '100%';
                done = true;
                clearInterval(poll);
                clearTimeout(hard_timeout);
                setTimeout(() => location.reload(), 12000);
            } else if (s.state === 4) {   /* FAILED */
                clearTimeout(hard_timeout);
                finish_failed(s.message);
            }
        } catch (e) {
            /* /ota/status didn't answer this poll. This might be:
             *   a) module rebooted (=new firmware still coming up), OR
             *   b) a plain network hiccup during a long download.
             * Only conclude 'reboot' after several consecutive misses
             * so a single Wi-Fi flap doesn't flip the UI to 'rebooting'
             * while the OTA is still writing to flash. Poll every
             * 800 ms => need 5 misses = ~4 s of silence before we act. */
            consecutive_fetch_fails++;
            const elapsed = (performance.now() - start) / 1000;
            const looks_rebooted =
                consecutive_fetch_fails >= 5 && (last_written > 0 || elapsed > 15);
            if (looks_rebooted && !done) {
                done = true;
                clearInterval(poll);
                clearTimeout(hard_timeout);
                text.textContent = '🔄 Module en reboot, page rechargée bientôt';
                setTimeout(() => location.reload(), 10000);
            }
        }
    }, 800);
}

/* --- Firmware logs (=ESP_LOG stream) --- */
async function loadFirmwareLogs() {
    try {
        const r = await fetch('/debug/logs');
        const txt = await r.text();
        const pre = $('firmware-log');
        const prevScroll = pre.scrollTop;
        const prevMax = pre.scrollHeight;
        pre.textContent = txt || '(aucune ligne pour l\'instant)';
        $('logs-counter').textContent = `${txt.length} octets`;
        if ($('logs-autoscroll').checked) {
            pre.scrollTop = pre.scrollHeight;
        } else {
            /* Preserve user's manual scroll position when autoscroll is OFF.
             * setting textContent resets scrollTop, so restore explicitly. */
            pre.scrollTop = prevScroll;
        }
    } catch (e) { /* silent, Wi-Fi flap during reboot */ }
}

/* --- Debug Micronova bus --- */

const DIR_LABEL = ['RX-READ ', 'RX-WRITE', 'TX-REPLY'];
const DIR_CLASS = ['dir-rx-read', 'dir-rx-write', 'dir-tx-reply'];
let debug_paused_scroll = false;

async function loadDebug() {
    try {
        const r = await fetch('/debug/uart');
        const j = await r.json();
        $('debug-counter').textContent = `${j.seq || 0} frames au total`;
        const pre = $('debug-log');
        if (!j.frames || j.frames.length === 0) {
            pre.textContent = '(en attente de trames...)';
            return;
        }
        const prevScroll = pre.scrollTop;
        pre.innerHTML = j.frames.map(f => {
            const t = String(f.t_ms).padStart(9, ' ');
            const d = DIR_LABEL[f.dir] || '???';
            const a = f.addr.toString(16).padStart(2, '0');
            const v = f.data.toString(16).padStart(2, '0');
            const cls = DIR_CLASS[f.dir] || '';
            return `<span class="${cls}">[${t} ms] ${d}  addr=0x${a}  data=0x${v}</span>`;
        }).join('\n');
        if ($('debug-autoscroll').checked && !debug_paused_scroll) {
            pre.scrollTop = pre.scrollHeight;
        } else {
            /* Preserve user's scroll position when autoscroll is off. */
            pre.scrollTop = prevScroll;
        }
    } catch (e) { /* Wi-Fi flap during reboot */ }
}

/* Init */
document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    initTabs();

    $('scan').addEventListener('click', scan);

    /* Test MQTT connection against the values currently in the form */
    $('mqtt-test').addEventListener('click', async () => {
        const btn = $('mqtt-test');
        const span = $('mqtt-test-result');
        const orig = btn.textContent;
        btn.disabled = true;
        btn.textContent = '⏳ Test...';
        span.textContent = '';
        try {
            const body = {
                host: $('mqtt-host').value.trim(),
                port: parseInt($('mqtt-port').value, 10) || 1883,
                user: $('mqtt-user').value,
                pwd:  $('mqtt-pwd').value,       /* empty = server uses stored */
                tls:  $('mqtt-tls').checked,
            };
            const r = await fetch('/mqtt/test', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(body),
            }).then(x => x.json());
            span.textContent = r.ok ? `✅ ${r.message}` : `❌ ${r.message}`;
            toast(r.message, r.ok ? 'success' : 'error', 6000);
        } catch (e) {
            span.textContent = '❌ Erreur réseau';
            toast('Erreur pendant le test : ' + e.message, 'error');
        } finally {
            btn.disabled = false;
            btn.textContent = orig;
        }
    });

    /* Wire the MQTT broker mDNS discovery button */
    $('mqtt-discover').addEventListener('click', async () => {
        const btn = $('mqtt-discover');
        const orig = btn.textContent;
        btn.disabled = true;
        btn.textContent = '⏳ mDNS...';
        try {
            const r = await j('/mqtt/discover');
            if (r.host) {
                $('mqtt-host').value = r.host;
                if (r.port) $('mqtt-port').value = r.port;
                toast(`✅ Broker détecté : ${r.host}:${r.port}`, 'success');
            } else {
                toast(r.message || 'Aucun broker MQTT trouvé sur le LAN', 'warn', 5000);
            }
        } catch (e) {
            toast('Erreur mDNS : ' + e.message, 'error');
        } finally {
            btn.disabled = false;
            btn.textContent = orig;
        }
    });
    /* Wire ssid-select once at load so the wifi-ssid text input auto-fills
     * whenever the user picks a scanned SSID. Attaching this handler inside
     * scan() re-attached it every rescan (=stacked handlers) and could
     * miss the first pick in some browsers. */
    $('ssid-select').addEventListener('change', () => {
        const v = $('ssid-select').value;
        if (v) $('wifi-ssid').value = v;
    });
    $('save').addEventListener('click', save);
    $('factory').addEventListener('click', factory);
    $('reboot').addEventListener('click', reboot);

    $('stove-on').addEventListener('click', () => stoveCmd('on'));
    $('stove-off').addEventListener('click', () => stoveCmd('off'));
    $('stove-reset').addEventListener('click', () => stoveCmd('reset_alarm'));

    $('ota-file').addEventListener('change', e => {
        if (e.target.files[0]) otaUpload(e.target.files[0]);
    });
    $('ota-rollback').addEventListener('click', otaRollback);
    $('ota-pull').addEventListener('click', otaPull);
    $('ota-check').addEventListener('click', otaCheck);

    $('debug-clear').addEventListener('click', () => {
        $('debug-log').textContent = '(vidé, prochain sondage dans <1 s)';
    });
    setInterval(loadDebug, 1000);
    setInterval(loadFirmwareLogs, 2000);

    const guardianEl = $('guardian-enabled');
    if (guardianEl) guardianEl.addEventListener('change', toggleGuardianSettings);

    loadConfig();
    loadStatus();
    setInterval(loadStatus, 5000);
    /* Live register table */
    async function loadRegs() {
        try {
            const d = await j('/api/registers');
            const el = document.getElementById('regs-live');
            if (!el) return;
            const rows = d.registers
                .filter(r => r.polled)
                .map(r => `  0x${r.addr.toString(16).toUpperCase().padStart(2,'0')} ${r.name.padEnd(14)} = ${String(r.raw).padStart(4)}  <span style="color:#888">${r.hint||''}</span>`)
                .join('<br>');
            el.innerHTML = rows;
        } catch (e) {}
    }
    loadRegs();
    setInterval(loadRegs, 2000);
});
