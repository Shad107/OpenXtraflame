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
        $('m-power').textContent = s.stove?.power ?? '-';
        $('m-fumi').textContent = s.stove?.t_smoke?.toFixed?.(0) ?? '-';

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
        toast('Connexion perdue pendant l\'upload — retente', 'error', 6000);
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

/* OTA state enum from firmware ota.h — kept aligned by convention. */
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
    const start = performance.now();
    const poll = setInterval(async () => {
        try {
            const s = await fetch('/ota/status').then(r => r.json());
            const label = OTA_STATE_LABEL[s.state] || `État ${s.state}`;
            const pct = s.total > 0
                ? Math.min(99, Math.floor(100 * s.written / s.total))
                : 5;
            bar.style.width = pct + '%';
            text.textContent = `${label} (${pct}%)`;
            if (s.state === 3) {          /* REBOOTING */
                text.textContent = '🔄 Reboot en cours, patiente ~10 s...';
                bar.style.width = '100%';
                done = true;
                clearInterval(poll);
                setTimeout(() => location.reload(), 12000);
            } else if (s.state === 4) {   /* FAILED */
                done = true;
                clearInterval(poll);
                toast(`❌ Échec OTA : ${s.message || 'raison inconnue'}`, 'error', 8000);
                btn.textContent = '⬇️ Pull';
                btn.disabled = false;
                progress.style.display = 'none';
            }
        } catch (e) {
            /* Wi-Fi flap right after reboot: assume success and reload. */
            const elapsed = (performance.now() - start) / 1000;
            if (elapsed > 15 && !done) {
                done = true;
                clearInterval(poll);
                text.textContent = '🔄 Module en reboot, page rechargée bientôt';
                setTimeout(() => location.reload(), 8000);
            }
        }
    }, 800);
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
        }
    } catch (e) { /* Wi-Fi flap during reboot */ }
}

/* Init */
document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    initTabs();

    $('scan').addEventListener('click', scan);
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

    const guardianEl = $('guardian-enabled');
    if (guardianEl) guardianEl.addEventListener('change', toggleGuardianSettings);

    loadConfig();
    loadStatus();
    setInterval(loadStatus, 5000);
});
