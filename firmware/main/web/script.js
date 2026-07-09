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
        if ($('about-idf') && c.idf_version) $('about-idf').textContent = 'ESP-IDF ' + c.idf_version;
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

    initMaintTab();
});

/* ============== ONGLET MAINTENANCE ============== */
/* Table Pr01-Pr30 factory Micronova I023 aria (=carte Teodora Evo I_VENT).
 * Sources : PDF Sercatec MANUAL-AIRE-LED + doc communauté. Encodage confirmé :
 * coclea ×10 (=byte 3 → 0.3s), échangeur en index 1-35, aspiration 16-bit
 * (=LSB seul lisible byte-à-byte, MSB au +1). */
const PR_TABLE = [
    {pr:'Pr01', addr:0x40, label:'Tempo max ciclo accensione', unit:'min', factory:15, scale:1, range:[1,18], safety:'safe'},
    {pr:'Pr02', addr:0x41, label:'Stabilisation flamme FIRE_ON', unit:'min', factory:6,  scale:1, range:[1,15], safety:'safe'},
    {pr:'Pr03', addr:0x42, label:'Intervalle nettoyage brasero', unit:'min', factory:60, scale:1, range:[10,90], safety:'safe'},
    {pr:'Pr04', addr:0x43, label:'Coclea LOAD_WOOD', unit:'s', factory:19, scale:0.1, range:[2,30], safety:'combustion'},
    {pr:'Pr05', addr:0x44, label:'Coclea FIRE_ON', unit:'s', factory:20, scale:0.1, range:[2,30], safety:'combustion'},
    {pr:'Pr06', addr:0x45, label:'Coclea puissance 1', unit:'s', factory:19, scale:0.1, range:[2,30], safety:'combustion'},
    {pr:'Pr07', addr:0x46, label:'Coclea puissance 2', unit:'s', factory:22, scale:0.1, range:[3,30], safety:'combustion'},
    {pr:'Pr08', addr:0x47, label:'Coclea puissance 3', unit:'s', factory:29, scale:0.1, range:[4,45], safety:'combustion'},
    {pr:'Pr09', addr:0x48, label:'Coclea puissance 4', unit:'s', factory:35, scale:0.1, range:[5,50], safety:'combustion'},
    {pr:'Pr10', addr:0x49, label:'Coclea puissance 5', unit:'s', factory:45, scale:0.1, range:[5,60], safety:'combustion'},
    {pr:'Pr11', addr:0x4A, label:'Retard alarmes', unit:'s', factory:240, scale:1, range:[30,240], safety:'danger'},
    {pr:'Pr12', addr:0x4B, label:'Durée nettoyage brasero', unit:'s', factory:30, scale:1, range:[0,240], safety:'safe'},
    {pr:'Pr13', addr:0x4C, label:'T° min fumées poêle allumé', unit:'°C', factory:50, scale:1, range:[40,120], safety:'danger'},
    {pr:'Pr14', addr:0x4D, label:'Seuil ECO-MODULA fumées', unit:'°C', factory:260, scale:1, range:[130,260], safety:'combustion'},
    {pr:'Pr15', addr:0x4E, label:'Seuil active échangeur', unit:'°C', factory:100, scale:1, range:[40,110], safety:'safe'},
    {pr:'Pr16', addr:0x4F, label:'Vitesse aspiration allumage (LSB)', unit:'tr/min', factory:1850, scale:1, range:[600,2780], safety:'combustion'},
    {pr:'Pr17', addr:0x50, label:'Vitesse aspiration démarrage (LSB)', unit:'tr/min', factory:1800, scale:1, range:[600,2780], safety:'combustion'},
    {pr:'Pr18', addr:0x51, label:'Vitesse aspiration P1 (LSB)', unit:'tr/min', factory:1850, scale:1, range:[600,2780], safety:'combustion'},
    {pr:'Pr19', addr:0x52, label:'Vitesse aspiration P2 (LSB)', unit:'tr/min', factory:1950, scale:1, range:[600,2780], safety:'combustion'},
    {pr:'Pr20', addr:0x53, label:'Vitesse aspiration P3 (LSB)', unit:'tr/min', factory:2100, scale:1, range:[600,2780], safety:'combustion'},
    {pr:'Pr21', addr:0x54, label:'Vitesse aspiration P4 (LSB)', unit:'tr/min', factory:2250, scale:1, range:[600,2780], safety:'combustion'},
    {pr:'Pr22', addr:0x55, label:'Vitesse aspiration P5 (LSB)', unit:'tr/min', factory:2350, scale:1, range:[600,2780], safety:'combustion'},
    {pr:'Pr23', addr:0x56, label:'Vitesse échangeur 1 P1', unit:'idx', factory:12, scale:1, range:[1,23], safety:'safe'},
    {pr:'Pr24', addr:0x57, label:'Vitesse échangeur 1 P2', unit:'idx', factory:15, scale:1, range:[2,26], safety:'safe'},
    {pr:'Pr25', addr:0x58, label:'Vitesse échangeur 1 P3', unit:'idx', factory:17, scale:1, range:[3,30], safety:'safe'},
    {pr:'Pr26', addr:0x59, label:'Vitesse échangeur 1 P4', unit:'idx', factory:19, scale:1, range:[5,35], safety:'safe'},
    {pr:'Pr27', addr:0x5A, label:'Vitesse échangeur 1 P5', unit:'idx', factory:21, scale:1, range:[7,35], safety:'safe'},
    {pr:'Pr28', addr:0x5B, label:'Seuil temp arrêt', unit:'°C', factory:50, scale:1, range:[40,120], safety:'danger'},
    {pr:'Pr29', addr:0x5C, label:'Aspiration nettoyage brasero', unit:'tr/min', factory:2200, scale:1, range:[700,2800], safety:'combustion'},
    {pr:'Pr30', addr:0x5D, label:'Coclea nettoyage', unit:'s', factory:15, scale:0.1, range:[0,40], safety:'combustion'},
];

const SAFETY_BADGE = {
    'safe':       '<span style="background:#10b981;color:white;padding:1px 6px;border-radius:3px;font-size:10px">SAFE</span>',
    'combustion': '<span style="background:#f59e0b;color:white;padding:1px 6px;border-radius:3px;font-size:10px">COMBUSTION</span>',
    'danger':     '<span style="background:#ef4444;color:white;padding:1px 6px;border-radius:3px;font-size:10px">DANGER</span>',
};

function initMaintTab() {
    /* Charger sur clic du sous-onglet */
    document.querySelectorAll('.subtab').forEach(t => {
        if (t.dataset.subtab === 'maint') {
            t.addEventListener('click', () => loadMaintenance());
        }
    });
    /* Boutons */
    const bind = (id, fn) => { const el = $(id); if (el) el.addEventListener('click', fn); };
    bind('m-reset-service', async () => {
        try { await fetch('/api/maint/reset_service', {method:'POST'});
            $('m-reset-result').textContent = '✓ Reset OK'; loadMaintenance();
        } catch(e) { $('m-reset-result').textContent = '✗ ' + e.message; }
    });
    bind('m-reset-cleaning', async () => {
        try { await fetch('/api/maint/reset_cleaning', {method:'POST'});
            $('m-reset-result').textContent = '✓ Reset OK'; loadMaintenance();
        } catch(e) { $('m-reset-result').textContent = '✗ ' + e.message; }
    });
    bind('m-snap-eeprom', async () => {
        try {
            const r = await fetch('/api/eeprom/snapshot');
            const data = await r.json();
            const blob = new Blob([JSON.stringify(data, null, 2)], {type:'application/json'});
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url; a.download = `eeprom-snapshot-${Date.now()}.json`;
            a.click(); URL.revokeObjectURL(url);
            $('m-snap-result').textContent = '✓ Snapshot téléchargé (' + data.count + ' bytes)';
        } catch(e) { $('m-snap-result').textContent = '✗ ' + e.message; }
    });
    bind('m-refresh-params', () => loadMaintenance());
    bind('m-rollback', async () => {
        if (!confirm('Rollback vers version firmware précédente ? Le module va redémarrer.')) return;
        try { await fetch('/ota/rollback', {method:'POST'});
            $('m-rollback-result').textContent = '✓ Rollback lancé, reboot en cours...';
        } catch(e) { $('m-rollback-result').textContent = '✗ ' + e.message; }
    });
}

async function loadMaintenance() {
    try {
        const [status, params, hist] = await Promise.all([
            j('/status.json'),
            j('/api/params/tech'),
            j('/api/history/alarms'),
        ]);
        renderMaintCounters(status.stove || {});
        renderMaintParams(params.params || []);
        renderMaintAlarms(hist || []);
        renderMaintDiag(status.stove || {}, params.params || []);
    } catch(e) {
        $('maint-diag').innerHTML = '<div style="color:#ef4444">Erreur chargement : ' + e.message + '</div>';
    }
}

function renderMaintCounters(s) {
    const set = (id, v) => { const el = $(id); if (el) el.textContent = (v ?? '-'); };
    set('m-h-since',   s.hours_since_service);
    set('m-h-before',  s.hours_before_service);
    set('m-s-since',   s.starts_since_cleaning);
    set('m-s-before',  s.starts_before_cleaning);
}

function renderMaintParams(params) {
    const tbody = $('m-params-tbody');
    if (!tbody) return;
    const byAddr = {};
    params.forEach(p => byAddr[p.eep_addr] = p.value);
    tbody.innerHTML = PR_TABLE.map(r => {
        const raw = byAddr[r.addr];
        const real = raw != null ? (raw * r.scale).toFixed(r.scale < 1 ? 1 : 0) : '-';
        const fac  = (r.factory * r.scale).toFixed(r.scale < 1 ? 1 : 0);
        let deltaPct = '-', deltaColor = '#666';
        if (raw != null && r.factory > 0) {
            const pct = ((raw - r.factory) / r.factory * 100);
            deltaPct = (pct > 0 ? '+' : '') + pct.toFixed(0) + '%';
            if (Math.abs(pct) < 10) deltaColor = '#10b981';
            else if (Math.abs(pct) < 30) deltaColor = '#f59e0b';
            else deltaColor = '#ef4444';
        }
        const editable = raw != null && r.safety !== 'danger';
        const editBtn = editable ? `<button data-addr="${r.addr}" data-cur="${raw}" data-fac="${r.factory}" data-label="${r.pr} ${r.label}" data-safety="${r.safety}" class="btn-edit-pr" style="padding:2px 6px;font-size:11px;background:#3b82f6;color:white;border:none;border-radius:3px;cursor:pointer">✏️</button>` : '';
        return `<tr style="border-bottom:1px solid #eee">
            <td style="padding:4px 6px;font-weight:bold">${r.pr}</td>
            <td style="padding:4px 6px">0x${r.addr.toString(16).padStart(2,'0')}</td>
            <td style="padding:4px 6px">${r.label}</td>
            <td style="padding:4px 6px;text-align:right">${real} ${r.unit}</td>
            <td style="padding:4px 6px;text-align:right;color:#888">${fac} ${r.unit}</td>
            <td style="padding:4px 6px;text-align:right;color:${deltaColor};font-weight:bold">${deltaPct}</td>
            <td style="padding:4px 6px">${SAFETY_BADGE[r.safety]} ${editBtn}</td>
        </tr>`;
    }).join('');

    /* Wire boutons Edit */
    tbody.querySelectorAll('.btn-edit-pr').forEach(btn => {
        btn.addEventListener('click', () => editPrParam(btn.dataset));
    });
}

async function editPrParam(d) {
    const addr = parseInt(d.addr);
    const cur = parseInt(d.cur);
    const fac = parseInt(d.fac);
    const raw = prompt(
        `Modifier ${d.label}\n\n` +
        `Actuel : ${cur}\n` +
        `Factory Micronova I023 aria : ${fac}\n` +
        `Zone : ${d.safety.toUpperCase()}\n\n` +
        `Nouvelle valeur (0-255) :`,
        String(fac));
    if (raw === null) return;
    const val = parseInt(raw, 10);
    if (isNaN(val) || val < 0 || val > 255) {
        alert('Valeur invalide (0-255 requis)');
        return;
    }
    const warn = d.safety === 'combustion'
        ? `⚠️ COMBUSTION : cette modif change le comportement combustion.\n`
        + `Vérifie la vitre + flamme pendant 30 min après application.\n\n`
        : `Zone SAFE : ajustement modéré recommandé.\n\n`;
    if (!confirm(warn + `Confirmer écriture Pr addr=0x${addr.toString(16)} valeur=${val} ?`)) return;
    try {
        const r = await fetch('/api/params/tech/write', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({addr, value: val}),
        });
        const j = await r.json();
        if (j.ok) {
            alert(`✓ Écriture queue. Valeur sera confirmée au prochain poll cycle.`);
            setTimeout(loadMaintenance, 3000);
        } else {
            alert(`✗ Écriture refusée : ${JSON.stringify(j)}`);
        }
    } catch(e) {
        alert(`✗ Erreur : ${e.message}`);
    }
}

const ALARM_LABELS = {
    1:  'Sonde fumées défectueuse',
    2:  'Fumées trop chaudes',
    4:  'Fumées court-circuit',
    8:  'Aspirateur défectueux',
    16: 'Échec allumage',
    32: 'Perte de flamme',
    64: 'Dépression insuffisante',
    128:'Commande vis sans fin',
};

function renderMaintAlarms(events) {
    const tbody = $('m-alarms-tbody');
    if (!tbody) return;
    if (!events.length) {
        tbody.innerHTML = '<tr><td colspan="5" style="padding:8px;color:#10b981">✓ Aucune alarme historisée</td></tr>';
        return;
    }
    const fmt = ts => ts ? new Date(ts*1000).toLocaleString('fr-FR', {day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'}) : '-';
    const dur = (a,b) => (a && b && b > a) ? Math.round((b-a)/60) + ' min' : '-';
    tbody.innerHTML = events.slice().reverse().map(e => {
        const label = Object.entries(ALARM_LABELS).find(([bit]) => e.code & parseInt(bit))?.[1] || `code ${e.code}`;
        return `<tr style="border-bottom:1px solid #eee">
            <td style="padding:4px 6px">${fmt(e.ts_start)}</td>
            <td style="padding:4px 6px">${fmt(e.ts_end)}</td>
            <td style="padding:4px 6px">${dur(e.ts_start, e.ts_end)}</td>
            <td style="padding:4px 6px"><code>0x${e.code.toString(16).padStart(2,'0')}</code></td>
            <td style="padding:4px 6px">${label}</td>
        </tr>`;
    }).join('');
}

/* Auto-diagnostic data-driven. Règles basées sur observations combustion. */
function renderMaintDiag(stove, params) {
    const byAddr = {};
    params.forEach(p => byAddr[p.eep_addr] = p.value);
    const diagnostics = [];

    /* Combustion : coclea vs factory */
    const cocleaAvg = ([0x43,0x44,0x47].map(a => byAddr[a] || 0).reduce((a,b)=>a+b,0) / 3);
    const cocleaFacAvg = ([19,20,29].reduce((a,b)=>a+b,0) / 3);
    if (cocleaAvg > 0 && cocleaAvg < cocleaFacAvg * 0.5) {
        diagnostics.push({sev:'critical', icon:'🔥', title:'Coclea (vis pellet) fortement sous factory',
            detail:`Moyenne coclea = ${(cocleaAvg/10).toFixed(1)}s vs factory ${(cocleaFacAvg/10).toFixed(1)}s. Combustion probablement maigre (=trop d'air, pas assez pellets). Symptômes typiques : vitre noire rapidement, brasero se salit vite, rendement dégradé.`,
            reco:'Remonter progressivement Pr04-Pr08 vers valeurs factory (+30% par étape, tester 24h).'});
    }

    /* Nettoyage brasero fréquent */
    if (byAddr[0x42] && byAddr[0x42] < 40) {
        diagnostics.push({sev:'warning', icon:'🧹', title:'Nettoyage brasero très fréquent',
            detail:`Pr03 = ${byAddr[0x42]} min (défaut 60 min). Signal combustion salissante.`,
            reco:'Corriger Pr04-Pr08 (=coclea) d\'abord, puis remonter Pr03 vers 60 min.'});
    }

    /* Ventilo échangeur */
    const echAvg = ([0x56,0x57,0x58].map(a => byAddr[a] || 0).reduce((a,b)=>a+b,0) / 3);
    const echFacAvg = (12 + 15 + 17) / 3;
    if (echAvg > echFacAvg * 1.8) {
        diagnostics.push({sev:'info', icon:'💨', title:'Vitesse échangeur (ventilo) élevée',
            detail:`Moyenne Pr23-Pr25 = ${echAvg.toFixed(1)} vs factory ${echFacAvg.toFixed(1)}. Ventilo poussé pour diffuser plus, souvent en compensation combustion pauvre.`,
            reco:'Si combustion corrigée, tu pourras baisser vers factory pour moins de bruit.'});
    }

    /* Fumées température */
    if (stove.t_smoke > 280) {
        diagnostics.push({sev:'warning', icon:'🌡️', title:'Températures fumées élevées',
            detail:`T fumées ${stove.t_smoke}°C > 280°C. Trop d'air excédent = gaspillage énergie.`,
            reco:'Baisser Pr16-Pr22 (=vitesse aspiration) ou augmenter Pr04-Pr08 (=coclea).'});
    } else if (stove.t_smoke > 0 && stove.t_smoke < 130) {
        diagnostics.push({sev:'warning', icon:'🌡️', title:'Températures fumées basses',
            detail:`T fumées ${stove.t_smoke}°C < 130°C. Risque condensation + encrassement conduit.`,
            reco:'Vérifier étanchéité + augmenter Pr16-Pr22 (=aspiration).'});
    }

    /* Alarmes récurrentes vérifiables ailleurs */
    if (stove.alarm_code && stove.alarm_code !== 0) {
        diagnostics.push({sev:'critical', icon:'🚨', title:'Alarme active',
            detail:`Code alarme actuel: 0x${stove.alarm_code.toString(16)}.`,
            reco:'Voir historique + résoudre cause avant modifications.'});
    }

    if (!diagnostics.length) {
        $('maint-diag').innerHTML = '<div style="color:#10b981;padding:8px;background:#ecfdf5;border-radius:4px">✓ Aucun problème détecté par le diagnostic auto. Consulte la table Pr01-Pr30 pour ajustements fins.</div>';
        return;
    }

    const sevColors = {'critical':'#ef4444','warning':'#f59e0b','info':'#3b82f6'};
    $('maint-diag').innerHTML = diagnostics.map(d => `
        <div style="border-left:4px solid ${sevColors[d.sev]};background:#fafafa;padding:10px 12px;border-radius:4px">
            <div style="font-weight:bold;margin-bottom:4px">${d.icon} ${d.title}</div>
            <div style="font-size:13px;color:#555;margin-bottom:4px">${d.detail}</div>
            <div style="font-size:13px;color:#0369a1"><strong>Reco:</strong> ${d.reco}</div>
        </div>
    `).join('');
}
