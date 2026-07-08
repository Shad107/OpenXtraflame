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
    /* Sous-onglets (=inside Poêle) */
    $$('.subtab').forEach(t => {
        t.addEventListener('click', () => {
            $$('.subtab').forEach(x => {
                x.classList.remove('active');
                x.style.borderBottomColor = 'transparent';
            });
            $$('.subtab-content').forEach(x => x.style.display = 'none');
            t.classList.add('active');
            t.style.borderBottomColor = '#ea580c';
            document.getElementById('subtab-' + t.dataset.subtab).style.display = 'block';
        });
    });
    /* Init : first subtab active border */
    const firstSub = document.querySelector('.subtab.active');
    if (firstSub) firstSub.style.borderBottomColor = '#ea580c';
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
        if ($('m-setpoint')) $('m-setpoint').textContent = s.stove?.setpoint ?? '-';
        /* Pré-remplit les inputs éditables sans écraser si l'user est en train de taper */
        if ($('m-setpoint-edit') && document.activeElement !== $('m-setpoint-edit') && s.stove?.setpoint != null)
            $('m-setpoint-edit').value = s.stove.setpoint;
        const p = s.stove?.power ?? null;
        $('m-power').textContent = p == null ? '-' : String(p);
        if ($('m-power-edit') && document.activeElement !== $('m-power-edit') && p != null && p >= 1 && p <= 5)
            $('m-power-edit').value = String(p);
        const pr = s.stove?.power_real ?? null;
        if ($('m-power-real')) $('m-power-real').textContent = pr == null ? '-' : String(pr);
        $('m-fumi').textContent = s.stove?.t_smoke?.toFixed?.(0) ?? '-';
        /* Onglet Consommation */
        const st = s.stove;
        if ($('m-pellets-total') && st?.pellets_total_kg != null) $('m-pellets-total').textContent = st.pellets_total_kg.toFixed(0);
        if ($('m-hours-total')   && st?.hours_total != null) $('m-hours-total').textContent = st.hours_total;
        if ($('c-total') && st?.pellets_total_kg != null) $('c-total').textContent = st.pellets_total_kg.toFixed(0);
        if ($('c-cost')  && st?.pellets_cost_lifetime_eur != null) $('c-cost').textContent = st.pellets_cost_lifetime_eur.toFixed(2);
        if ($('c-hours') && st?.hours_total != null) $('c-hours').textContent = st.hours_total;
        if ($('c-sacs')  && st?.pellets_total_kg != null) {
            const sackSize = parseFloat($('pl-sack')?.value) || 15;
            $('c-sacs').textContent = Math.round(st.pellets_total_kg / sackSize);
        }
        if ($('c-breakdown') && st) {
            const conso = [
                parseFloat($('pl-c1')?.value)||0.586,
                parseFloat($('pl-c2')?.value)||0.879,
                parseFloat($('pl-c3')?.value)||1.172,
                parseFloat($('pl-c4')?.value)||1.465,
                parseFloat($('pl-c5')?.value)||1.758
            ];
            const hours = [st.hours_p1||0, st.hours_p2||0, st.hours_p3||0, st.hours_p4||0, st.hours_p5||0];
            const totals = hours.map((h,i) => h * conso[i]);
            const sum = totals.reduce((a,b)=>a+b, 0);
            $('c-breakdown').innerHTML = ['P1','P2','P3','P4','P5'].map((n,i) => {
                const pct = sum > 0 ? (totals[i]/sum*100).toFixed(1) : '0';
                return `<tr style="border-top:1px solid #eee"><td style="padding:6px 8px"><strong>${n}</strong></td><td style="padding:6px 8px">${hours[i]}</td><td style="padding:6px 8px">${conso[i].toFixed(2)}</td><td style="padding:6px 8px">${totals[i].toFixed(0)}</td><td style="padding:6px 8px">${pct}%</td></tr>`;
            }).join('');
        }
        /* Enum Extraflame TotalControl (0x21 machineState) */
        const STATES = ['Off','Check up','Ignition','Préparation','Préchargement','Modulation','En marche','Nettoyage','Refroidissement','Veille','Nettoyage final','Recovery','Allumage final'];
        if ($('m-state')) {
            const st = s.stove?.state ?? null;
            $('m-state').textContent = st == null ? '-' : (STATES[st] || 'état ' + st);
        }
        /* Stove identity info (=Phase 3 auto-detection) */
        if ($('info-stove-type'))  $('info-stove-type').textContent  = s.stove?.stove_type  || '-';
        if ($('info-stove-model')) $('info-stove-model').textContent = s.stove?.stove_model || '(vide)';
        if ($('info-matricola'))   $('info-matricola').textContent   = s.stove?.matricola   || '-';

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
        /* Cache la carte cloud si build != TARGET_BLACKLABEL.
         * Cf. web_ui.c handle_config_get qui expose "target". */
        if (c.target && c.target !== 'blacklabel') {
            document.querySelectorAll('.bl-only').forEach(el => el.style.display = 'none');
        }
        /* Onglet À propos : badge target + version */
        if ($('about-target')) {
            $('about-target').textContent = c.target === 'blacklabel' ? '🔥 Black Label' : '📟 External';
            $('about-target').className = 'badge ' + (c.target === 'blacklabel' ? 'badge-success' : 'badge-info');
        }
        if ($('about-version') && c.version) $('about-version').textContent = c.version;
        $('wifi-ssid').value = c.wifi_ssid || '';
        $('mqtt-host').value = c.mqtt_host || '';
        $('mqtt-port').value = c.mqtt_port || 1883;
        $('mqtt-user').value = c.mqtt_user || '';
        $('mqtt-prefix').value = c.mqtt_prefix || 'extraflame';
        $('mqtt-tls').checked = !!c.mqtt_tls;
        $('stove-name').value = c.stove_name || 'poele';
        /* stove-type est maintenant en info readonly, alimenté par loadStatus */
        $('ha-discovery').checked = !!c.ha_discovery;
        $('publish-interval').value = c.publish_interval_ms || 5000;
        if ($('cloud-enabled')) $('cloud-enabled').checked = !!c.cloud_enabled;
        if ($('tc2-username'))    $('tc2-username').value    = c.tc2_username || '';
        if ($('tc2-password'))    $('tc2-password').placeholder = c.tc2_password_set ? '(mot de passe déjà enregistré)' : '(laisse vide pour ne pas changer)';
        if ($('tc2-stove-id'))    $('tc2-stove-id').value    = c.tc2_stove_id || '';
        if ($('tc2-stove-model')) $('tc2-stove-model').value = c.tc2_stove_model || '';
        /* Pellet config */
        const pl = c.pellet || {};
        if ($('pl-tank'))   $('pl-tank').value   = pl.tank_capacity_kg ?? 14;
        if ($('pl-sack'))   $('pl-sack').value   = pl.sack_size_kg ?? 15;
        if ($('pl-price'))  $('pl-price').value  = pl.price_per_sack ?? 6;
        if ($('pl-winter')) $('pl-winter').value = pl.winter_days ?? 180;
        if ($('pl-nom-kw')) $('pl-nom-kw').value = pl.stove_nominal_kw ?? 8.0;
        if ($('pl-min-kw')) $('pl-min-kw').value = pl.stove_min_kw ?? 2.5;
        if ($('pl-eff'))    $('pl-eff').value    = (pl.stove_efficiency ?? 90.8).toFixed(2);
        if ($('pl-cal'))    $('pl-cal').value    = (pl.pellet_calorific ?? 4.7).toFixed(2);
        function recalcConso() {
            const nom = parseFloat($('pl-nom-kw').value) || 8.0;
            const mn  = parseFloat($('pl-min-kw').value) || 2.5;
            const eff = parseFloat($('pl-eff').value) || 90.8;
            const cal = parseFloat($('pl-cal').value) || 4.7;
            const factor = 1.0 / ((eff/100) * cal);
            const p1 = mn * factor;
            const p5 = nom * factor;
            const step = (p5 - p1) / 4;
            $('pl-c1').value = p1.toFixed(2);
            $('pl-c2').value = (p1 + step).toFixed(2);
            $('pl-c3').value = (p1 + step*2).toFixed(2);
            $('pl-c4').value = (p1 + step*3).toFixed(2);
            $('pl-c5').value = p5.toFixed(2);
        }
        recalcConso();
        ['pl-nom-kw','pl-min-kw','pl-eff','pl-cal'].forEach(id => {
            const el = $(id);
            if (el && !el.dataset.bound) { el.addEventListener('input', recalcConso); el.dataset.bound = '1'; }
        });

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
        /* stove_type est auto-détecté (Phase 3), plus envoyé depuis le UI */
        ha_discovery: $('ha-discovery').checked,
        publish_interval_ms: parseInt($('publish-interval').value, 10),
        cloud_enabled: $('cloud-enabled') ? $('cloud-enabled').checked : false,
        tc2_username: $('tc2-username') ? $('tc2-username').value : '',
        tc2_password: $('tc2-password') ? $('tc2-password').value : '',
        pellet: {
            tank_capacity_kg: parseFloat($('pl-tank').value)  || 14,
            sack_size_kg:     parseFloat($('pl-sack').value)  || 15,
            price_per_sack:   parseFloat($('pl-price').value) || 6,
            winter_days:      parseInt  ($('pl-winter').value, 10) || 180,
            stove_nominal_kw: parseFloat($('pl-nom-kw').value) || 8.0,
            stove_min_kw:     parseFloat($('pl-min-kw').value) || 2.5,
            stove_efficiency: parseFloat($('pl-eff').value)   || 90.8,
            pellet_calorific: parseFloat($('pl-cal').value)   || 4.7,
        },
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
        const autoscroll = $('logs-autoscroll') && $('logs-autoscroll').checked;
        pre.textContent = txt || '(aucune ligne pour l\'instant)';
        $('logs-counter').textContent = `${txt.length} octets`;
        /* Use requestAnimationFrame to force scroll after layout reflow */
        requestAnimationFrame(() => {
            if (autoscroll) pre.scrollTop = pre.scrollHeight;
            else pre.scrollTop = prevScroll;
        });
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
        const autoscroll = $('debug-autoscroll') && $('debug-autoscroll').checked && !debug_paused_scroll;
        pre.innerHTML = j.frames.map(f => {
            const t = String(f.t_ms).padStart(9, ' ');
            const d = DIR_LABEL[f.dir] || '???';
            const a = f.addr.toString(16).padStart(2, '0');
            const v = f.data.toString(16).padStart(2, '0');
            const cls = DIR_CLASS[f.dir] || '';
            return `<span class="${cls}">[${t} ms] ${d}  addr=0x${a}  data=0x${v}</span>`;
        }).join('\n');
        requestAnimationFrame(() => {
            if (autoscroll) pre.scrollTop = pre.scrollHeight;
            else pre.scrollTop = prevScroll;
        });
    } catch (e) { /* Wi-Fi flap during reboot */ }
}

/* Init */
document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    initTabs();

    $('scan').addEventListener('click', scan);

    /* Safe mode : bouton d'urgence pour boot minimal (=juste OTA) */
    const safeBtn = $('safe-mode-btn');
    if (safeBtn) safeBtn.addEventListener('click', async () => {
        if (!await confirm('Activer safe mode et rebooter ?\nAu prochain boot, Micronova/MQTT/Cloud seront désactivés.\nUn 2e reboot ramène en mode normal.')) return;
        try {
            await fetch('/api/safe_mode', {method: 'POST'});
            safeBtn.textContent = '🔄 Reboot en cours...';
            safeBtn.disabled = true;
        } catch (e) { alert('Erreur : ' + e); }
    });

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
    /* Registres live retirés (=carte UI supprimée, /api/registers reste dispo debug) */
    /* Chrono live - éditable inline */
    let chronoState = null;
    async function loadChrono() {
        try {
            const d = await j('/api/chrono');
            chronoState = d;
            const masterCb = document.getElementById('chrono-master-cb');
            if (masterCb) masterCb.checked = !!d.master_enabled;
            const wrap = document.getElementById('chrono-programs');
            if (wrap && d.programs) {
                wrap.innerHTML = d.programs.map(p => {
                    const dayCbs = p.days.map((day, di) =>
                        `<label style="display:flex;align-items:center;gap:2px;font-size:12px">
                            <input type="checkbox" data-prog="${p.id-1}" data-day="${di}" ${day.enabled?'checked':''}>
                            ${day.name}
                         </label>`).join('');
                    return `<div style="border:1px solid #e0e0e0;border-radius:6px;padding:12px;background:#fafafa">
                        <div style="display:flex;align-items:center;gap:12px;margin-bottom:10px">
                            <strong style="font-size:16px">P${p.id}</strong>
                            <label style="display:flex;align-items:center;gap:6px">
                                <input type="checkbox" data-prog="${p.id-1}" data-field="enabled" ${p.enabled?'checked':''}>
                                Actif
                            </label>
                        </div>
                        <div style="display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:10px">
                            <label style="font-size:12px">Début
                                <input type="time" data-prog="${p.id-1}" data-field="start" value="${p.start}" style="width:100%">
                            </label>
                            <label style="font-size:12px">Fin
                                <input type="time" data-prog="${p.id-1}" data-field="stop" value="${p.stop}" style="width:100%">
                            </label>
                            <label style="font-size:12px">Consigne (°C)
                                <input type="number" min="1" max="30" data-prog="${p.id-1}" data-field="temp_c" value="${p.temp_c}" style="width:100%">
                            </label>
                        </div>
                        <div style="display:flex;gap:8px;flex-wrap:wrap">${dayCbs}</div>
                    </div>`;
                }).join('');
            }
        } catch (e) {}
    }
    document.getElementById('chrono-reload').addEventListener('click', loadChrono);
    document.getElementById('chrono-save').addEventListener('click', async () => {
        const resEl = document.getElementById('chrono-save-result');
        const payload = {
            master_enabled: document.getElementById('chrono-master-cb').checked,
            programs: [0,1,2,3].map(idx => {
                const inputs = document.querySelectorAll(`input[data-prog="${idx}"]`);
                const prog = { id: idx + 1, days: [] };
                for (let day = 0; day < 7; day++) prog.days.push(false);
                inputs.forEach(el => {
                    if (el.dataset.field === 'enabled') prog.enabled = el.checked;
                    else if (el.dataset.field === 'start') prog.start = el.value;
                    else if (el.dataset.field === 'stop')  prog.stop  = el.value;
                    else if (el.dataset.field === 'temp_c') prog.temp_c = parseInt(el.value, 10);
                    else if (el.dataset.day !== undefined) prog.days[parseInt(el.dataset.day, 10)] = el.checked;
                });
                return prog;
            })
        };
        resEl.textContent = 'Envoi...';
        try {
            const r = await fetch('/api/chrono', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(payload)
            });
            const j2 = await r.json();
            resEl.textContent = j2.ok ? `✅ ${j2.writes} écritures` : '❌ erreur';
            setTimeout(loadChrono, 2000);
        } catch (e) {
            resEl.textContent = '❌ ' + e.message;
        }
    });
    /* Setpoint + power inline edit (=envoi direct au module) */
    async function postCmd(url, ok, err) {
        try {
            const r = await fetch(url, {method:'POST'});
            const j2 = await r.json();
            if (j2.ok) { ok && ok(); }
            else { err && err(j2); }
        } catch(e) { err && err(e); }
    }
    if ($('m-setpoint-save')) {
        $('m-setpoint-save').addEventListener('click', () => {
            const v = parseInt($('m-setpoint-edit').value, 10);
            if (v>=1 && v<=40) postCmd(`/api/stove/setpoint/${v}`, () => setTimeout(loadStatus, 500));
        });
    }
    if ($('m-power-save')) {
        $('m-power-save').addEventListener('click', () => {
            const v = parseInt($('m-power-edit').value, 10);
            if (v>=1 && v<=5) postCmd(`/api/stove/power/${v}`, () => setTimeout(loadStatus, 500));
        });
    }
    loadChrono();
    /* Auto-refresh chrono toutes les 10s pour voir les changements HA/EEPROM */
    setInterval(loadChrono, 10000);
    /* Auto-refresh config (=inclut pellet) toutes les 15s */
    setInterval(async () => {
        try {
            const c = await j('/config.json');
            const pl = c.pellet || {};
            /* Ne recharge que si l'onglet Pellets n'est pas actif (=évite écraser modifs en cours) */
            const subtabPellet = document.querySelector('.subtab.active');
            if (!subtabPellet || subtabPellet.dataset.subtab !== 'config') {
                if ($('pl-tank'))   $('pl-tank').value   = pl.tank_capacity_kg ?? 14;
                if ($('pl-sack'))   $('pl-sack').value   = pl.sack_size_kg ?? 15;
                if ($('pl-price'))  $('pl-price').value  = pl.price_per_sack ?? 6;
                if ($('pl-winter')) $('pl-winter').value = pl.winter_days ?? 180;
                ['1','2','3','4','5'].forEach(n => {
                    const el = $('pl-c'+n);
                    if (el) el.value = (pl['consumption_p'+n] ?? [0.5,0.825,1.15,1.475,1.8][parseInt(n)-1]).toFixed(2);
                });
            }
        } catch(e) {}
    }, 15000);
});
