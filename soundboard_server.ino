/*
 * soundboard_server.ino
 * Nano ESP32 — Multi-page Web Control Panel for GooseBot
 *
 * Pages:
 *   Home       - landing page with 3 options
 *   Music      - Now Playing, transport (prev/pause/play/next), volume, track list
 *   Soundboard - Honks tab + Sounds tab, random buttons, volume, full grids
 *   Monitor    - Live telemetry: battery, RPMs, temps, IMU pitch, etc.
 *
 * Wiring (Nano ESP32 ↔ Mega Serial3):
 *   Nano D1 (TX)  →  Mega pin 15 (Serial3 RX)
 *   Mega pin 14 (Serial3 TX)  →  Nano D0 (RX)
 *   Shared GND
 *
 * Libraries:
 *   - ESPAsyncWebServer (lacamera)
 *   - AsyncTCP (dvarrel)
 */

 #include <WiFi.h>
 #include <AsyncTCP.h>
 #include <ESPAsyncWebServer.h>
 
 // ── Config ─────────────────────────────────────────────────────────────────
 const char* SSID     = "GooseBot";
 const char* PASSWORD = "quackquack";
 #define MEGA_SERIAL Serial1
 // ───────────────────────────────────────────────────────────────────────────
 
 AsyncWebServer server(80);
 
 // ── State (mirrors what's on the Mega) ─────────────────────────────────────
 int   currentTrack  = 0;
 bool  isPlaying     = false;
 int   currentVolume = 25;
 int   velL = 0, velR = 0;
 int   tempL = 0, tempR = 0;
 float voltage = 0;
 int   battPct = 0;
 bool  djMode = false, hopperOpen = false, isDead = false;
 float pitch = 0;
 // ───────────────────────────────────────────────────────────────────────────
 
 
 // ═════════════════════════════════════════════════════════════════════════════
 // HTML + CSS + JS (single-page app served from PROGMEM)
 // ═════════════════════════════════════════════════════════════════════════════
 const char PAGE[] PROGMEM = R"HTML(
 <!DOCTYPE html>
 <html lang="en">
 <head>
 <meta charset="UTF-8">
 <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
 <title>GooseBot</title>
 <style>
 @import url('https://fonts.googleapis.com/css2?family=Black+Ops+One&family=Share+Tech+Mono&display=swap');
 :root{--bg:#0a0a0f;--panel:#12121a;--bdr:#2a2a40;--acc:#00ffe0;--hot:#ff3c6e;
   --warn:#ffcc00;--good:#00ff88;--text:#e0e0f0;--dim:#555570}
 *{box-sizing:border-box;margin:0;padding:0}
 body{background:var(--bg);color:var(--text);font-family:'Share Tech Mono',monospace;
   min-height:100vh;padding:14px;
   background-image:radial-gradient(circle at 50% 0%,#00ffe014 0%,transparent 55%),
     radial-gradient(circle at 50% 100%,#ff3c6e0a 0%,transparent 55%)}
 .app{max-width:480px;margin:0 auto}
 .page{display:none}
 .page.active{display:flex;flex-direction:column;gap:10px}
 
 /* header */
 .hdr{display:flex;align-items:center;gap:10px;padding:6px 0}
 .hdr-back{background:var(--panel);border:1px solid var(--bdr);border-radius:8px;
   color:var(--acc);font-family:'Share Tech Mono',monospace;font-size:.85rem;
   padding:8px 12px;cursor:pointer;-webkit-tap-highlight-color:transparent;letter-spacing:.1em}
 .hdr-back:active{transform:scale(.94);box-shadow:0 0 8px var(--acc)}
 .hdr-title{font-family:'Black Ops One',sans-serif;font-size:1.3rem;
   letter-spacing:.12em;flex:1}
 
 /* home */
 .home-logo{font-family:'Black Ops One',sans-serif;font-size:clamp(2.5rem,12vw,3.5rem);
   letter-spacing:.15em;color:var(--acc);filter:drop-shadow(0 0 14px var(--acc));
   text-align:center;padding:30px 0 4px}
 .home-logo span{color:var(--text);filter:none}
 .home-sub{font-size:.65rem;color:var(--dim);letter-spacing:.35em;
   text-align:center;margin-bottom:24px}
 .home-card{background:var(--panel);border:1px solid var(--bdr);border-radius:12px;
   padding:18px;display:flex;align-items:center;gap:14px;cursor:pointer;
   transition:all .15s;-webkit-tap-highlight-color:transparent;position:relative;overflow:hidden}
 .home-card::before{content:'';position:absolute;left:0;top:0;bottom:0;width:3px;
   background:var(--acc);opacity:0;transition:opacity .15s}
 .home-card:active{transform:scale(.97);border-color:var(--acc);box-shadow:0 0 14px #00ffe066}
 .home-card:active::before{opacity:1}
 .home-card .icon{font-size:2rem}
 .home-card .title{font-family:'Black Ops One',sans-serif;font-size:1.1rem;
   letter-spacing:.1em;margin-bottom:2px}
 .home-card .desc{font-size:.65rem;color:var(--dim);letter-spacing:.05em}
 .home-card .arrow{color:var(--dim);margin-left:auto;font-size:1.4rem}
 
 /* now playing */
 .np{background:var(--panel);border:1px solid var(--acc);border-radius:10px;
   padding:14px 16px;position:relative;overflow:hidden}
 .np::before{content:'';position:absolute;inset:0;
   background:radial-gradient(ellipse at 30% 0%,#00ffe018 0%,transparent 70%);
   pointer-events:none}
 .np-label{font-size:.6rem;letter-spacing:.3em;color:var(--acc);margin-bottom:6px}
 .np-title{font-family:'Black Ops One',sans-serif;font-size:1.2rem;
   letter-spacing:.06em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
 .np-sub{font-size:.65rem;color:var(--dim);margin-top:4px;letter-spacing:.1em}
 .np.idle{border-color:var(--bdr)}
 .np.idle .np-label,.np.idle .np-title{color:var(--dim)}
 
 /* transport */
 .transport{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
 .transport button{background:var(--panel);border:1px solid var(--bdr);
   border-radius:9px;color:var(--text);font-size:1.3rem;padding:14px 0;
   cursor:pointer;transition:all .1s;-webkit-tap-highlight-color:transparent}
 .transport button.primary{border-color:var(--acc);color:var(--acc)}
 .transport button:active{transform:scale(.92);background:#001a16;
   box-shadow:0 0 10px var(--acc);border-color:var(--acc)}
 
 /* volume */
 .vol-block{background:var(--panel);border:1px solid var(--bdr);
   border-radius:10px;padding:12px 14px}
 .vol-label{font-size:.6rem;letter-spacing:.3em;color:var(--dim);
   margin-bottom:8px;display:flex;justify-content:space-between}
 .vol-label .val{color:var(--acc);font-family:'Black Ops One',sans-serif}
 .vol-row{display:flex;gap:8px;align-items:center}
 .vol-btn{background:var(--bg);border:1px solid var(--bdr);border-radius:6px;
   color:var(--text);font-size:1.1rem;width:38px;height:38px;cursor:pointer;
   -webkit-tap-highlight-color:transparent;flex-shrink:0}
 .vol-btn:active{transform:scale(.92);border-color:var(--acc);box-shadow:0 0 6px var(--acc)}
 .vol-slider{flex:1;height:6px;-webkit-appearance:none;appearance:none;
   background:var(--bdr);border-radius:3px;outline:none}
 .vol-slider::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;
   border-radius:50%;background:var(--acc);cursor:pointer;box-shadow:0 0 8px var(--acc)}
 .vol-slider::-moz-range-thumb{width:22px;height:22px;border-radius:50%;
   background:var(--acc);cursor:pointer;border:none;box-shadow:0 0 8px var(--acc)}
 
 /* section labels */
 .sec{font-size:.6rem;letter-spacing:.3em;color:var(--dim);
   border-bottom:1px solid var(--bdr);padding-bottom:4px;margin-top:4px}
 
 /* list items */
 .list{display:flex;flex-direction:column;gap:5px}
 .li{background:var(--panel);border:1px solid var(--bdr);border-radius:7px;
   padding:10px 12px;display:flex;align-items:center;gap:10px;cursor:pointer;
   transition:all .1s;-webkit-tap-highlight-color:transparent}
 .li:active{transform:scale(.99);border-color:var(--warn);background:#1a1400;
   box-shadow:0 0 8px #ffcc0044}
 .li.playing{border-color:var(--acc);background:#001a16}
 .li-num{font-size:.65rem;color:var(--dim);min-width:26px;
   font-family:'Black Ops One',sans-serif}
 .li-name{flex:1;font-size:.82rem}
 .li-icon{color:var(--dim);font-size:.85rem}
 .li.playing .li-icon{color:var(--acc)}
 
 /* tabs */
 .tabs{display:grid;grid-template-columns:1fr 1fr;gap:6px}
 .tab-btn{background:var(--panel);border:1px solid var(--bdr);border-radius:8px;
   color:var(--dim);font-family:'Black Ops One',sans-serif;font-size:.8rem;
   letter-spacing:.12em;padding:12px;cursor:pointer;
   -webkit-tap-highlight-color:transparent;transition:all .1s}
 .tab-btn.active{border-color:var(--acc);color:var(--acc);background:#001a16;
   box-shadow:0 0 8px #00ffe055}
 
 /* random button */
 .random-btn{background:linear-gradient(135deg,#001a16,#002a26);
   border:1px solid var(--acc);border-radius:10px;color:var(--acc);
   font-family:'Black Ops One',sans-serif;font-size:1rem;letter-spacing:.12em;
   padding:18px;cursor:pointer;-webkit-tap-highlight-color:transparent;
   box-shadow:0 0 12px #00ffe033;transition:all .12s}
 .random-btn:active{transform:scale(.97);box-shadow:0 0 18px var(--acc)}
 
 /* sound grid */
 .grid{display:grid;grid-template-columns:repeat(2,1fr);gap:6px}
 .grid .li{padding:11px 10px}
 
 /* monitor */
 .mon-grid{display:grid;grid-template-columns:1fr 1fr;gap:7px}
 .mon-card{background:var(--panel);border:1px solid var(--bdr);
   border-radius:10px;padding:13px;position:relative;overflow:hidden}
 .mon-card.full{grid-column:1/-1}
 .mon-label{font-size:.55rem;letter-spacing:.3em;color:var(--dim);margin-bottom:4px}
 .mon-val{font-family:'Black Ops One',sans-serif;font-size:1.4rem;letter-spacing:.05em}
 .mon-val .unit{font-size:.65rem;color:var(--dim);margin-left:3px}
 .mon-val.good{color:var(--good)}
 .mon-val.warn{color:var(--warn)}
 .mon-val.hot{color:var(--hot)}
 .batt-bar{height:8px;background:var(--bdr);border-radius:4px;
   margin-top:8px;overflow:hidden}
 .batt-fill{height:100%;background:var(--good);transition:all .4s;border-radius:4px}
 .batt-fill.warn{background:var(--warn)}
 .batt-fill.hot{background:var(--hot)}
 .mon-row{display:flex;justify-content:space-between;align-items:baseline;gap:8px}
 
 /* status bar */
 .sbar{display:flex;justify-content:space-between;font-size:.6rem;
   color:var(--dim);letter-spacing:.15em;padding:4px 0}
 .dot{width:7px;height:7px;border-radius:50%;background:var(--dim);
   display:inline-block;margin-right:5px;transition:all .3s}
 .sbar.ok .dot{background:var(--good);box-shadow:0 0 5px var(--good)}
 .sbar.err .dot{background:var(--hot)}
 
 /* footer */
 .foot{font-size:.55rem;color:var(--dim);letter-spacing:.25em;
   text-align:center;padding:16px 0 8px}
 
 /* dead banner */
 .dead-banner{background:var(--hot);color:var(--bg);
   font-family:'Black Ops One',sans-serif;font-size:1rem;letter-spacing:.2em;
   padding:14px;border-radius:8px;text-align:center;animation:db 1s infinite}
 @keyframes db{0%,50%,100%{opacity:1}25%,75%{opacity:.4}}
 </style>
 </head>
 <body>
 <div class="app">
 
   <!-- ════════════ HOME ════════════ -->
   <div id="page-home" class="page active">
     <div class="home-logo">🪿 GOOSE<span>BOT</span></div>
     <p class="home-sub">CONTROL UNIT</p>
 
     <div class="home-card" onclick="goto('music')">
       <div class="icon">🎵</div>
       <div>
         <div class="title">MUSIC</div>
         <div class="desc">tracks · transport · volume</div>
       </div>
       <div class="arrow">›</div>
     </div>
 
     <div class="home-card" onclick="goto('soundboard')">
       <div class="icon">🔊</div>
       <div>
         <div class="title">SOUNDBOARD</div>
         <div class="desc">honks · sound effects</div>
       </div>
       <div class="arrow">›</div>
     </div>
 
     <div class="home-card" onclick="goto('monitor')">
       <div class="icon">📊</div>
       <div>
         <div class="title">MONITOR</div>
         <div class="desc">live telemetry · diagnostics</div>
       </div>
       <div class="arrow">›</div>
     </div>
 
     <div class="sbar" id="sbar-home">
       <span><span class="dot"></span><span id="sb-home-txt">CONNECTING...</span></span>
       <span>v1.0</span>
     </div>
   </div>
 
   <!-- ════════════ MUSIC ════════════ -->
   <div id="page-music" class="page">
     <div class="hdr">
       <button class="hdr-back" onclick="goto('home')">‹ BACK</button>
       <div class="hdr-title">🎵 MUSIC</div>
     </div>
 
     <div class="np idle" id="np-music">
       <div class="np-label">▶ NOW PLAYING</div>
       <div class="np-title" id="np-title">— NOTHING —</div>
       <div class="np-sub" id="np-sub">tap a track to start</div>
     </div>
 
     <div class="transport">
       <button onclick="cmd('prev')" title="Previous">⏮</button>
       <button class="primary" onclick="cmd('pause')" title="Pause">⏸</button>
       <button class="primary" onclick="cmd('resume')" title="Play">▶</button>
       <button onclick="cmd('next')" title="Next">⏭</button>
     </div>
 
     <div class="vol-block">
       <div class="vol-label"><span>VOLUME</span><span class="val" id="vol-val-m">25</span></div>
       <div class="vol-row">
         <button class="vol-btn" onclick="cmd('voldown')">−</button>
         <input type="range" min="0" max="30" value="25" class="vol-slider" id="vol-slider-m"
           oninput="updateVolDisp(this.value)" onchange="setVol(this.value)">
         <button class="vol-btn" onclick="cmd('volup')">+</button>
       </div>
     </div>
 
     <div class="sec">🎧 ALL TRACKS</div>
     <div class="list" id="track-list"></div>
   </div>
 
   <!-- ════════════ SOUNDBOARD ════════════ -->
   <div id="page-soundboard" class="page">
     <div class="hdr">
       <button class="hdr-back" onclick="goto('home')">‹ BACK</button>
       <div class="hdr-title">🔊 SOUNDBOARD</div>
     </div>
 
     <div class="tabs">
       <button class="tab-btn active" id="tab-honks" onclick="setTab('honks')">🪿 HONKS</button>
       <button class="tab-btn" id="tab-sounds" onclick="setTab('sounds')">🎚️ SOUNDS</button>
     </div>
 
     <button class="random-btn" id="random-btn" onclick="playRandom()">🎲 RANDOM HONK</button>
 
     <div class="vol-block">
       <div class="vol-label"><span>VOLUME</span><span class="val" id="vol-val-s">25</span></div>
       <div class="vol-row">
         <button class="vol-btn" onclick="cmd('voldown')">−</button>
         <input type="range" min="0" max="30" value="25" class="vol-slider" id="vol-slider-s"
           oninput="updateVolDisp(this.value)" onchange="setVol(this.value)">
         <button class="vol-btn" onclick="cmd('volup')">+</button>
       </div>
     </div>
 
     <div class="sec" id="sec-label">🪿 ALL HONKS</div>
     <div class="grid" id="sound-grid"></div>
   </div>
 
   <!-- ════════════ MONITOR ════════════ -->
   <div id="page-monitor" class="page">
     <div class="hdr">
       <button class="hdr-back" onclick="goto('home')">‹ BACK</button>
       <div class="hdr-title">📊 MONITOR</div>
     </div>
 
     <div id="dead-banner-wrap"></div>
 
     <div class="mon-grid">
       <div class="mon-card full">
         <div class="mon-label">🔋 BATTERY</div>
         <div class="mon-row">
           <div class="mon-val" id="m-batt">— <span class="unit">%</span></div>
           <div class="mon-val" id="m-volts" style="font-size:1rem">— <span class="unit">V</span></div>
         </div>
         <div class="batt-bar"><div class="batt-fill" id="m-batt-fill" style="width:0%"></div></div>
       </div>
 
       <div class="mon-card">
         <div class="mon-label">⚙ RPM L</div>
         <div class="mon-val" id="m-rpm-l">—</div>
       </div>
       <div class="mon-card">
         <div class="mon-label">⚙ RPM R</div>
         <div class="mon-val" id="m-rpm-r">—</div>
       </div>
 
       <div class="mon-card">
         <div class="mon-label">🌡 TEMP L</div>
         <div class="mon-val" id="m-temp-l">— <span class="unit">°C</span></div>
       </div>
       <div class="mon-card">
         <div class="mon-label">🌡 TEMP R</div>
         <div class="mon-val" id="m-temp-r">— <span class="unit">°C</span></div>
       </div>
 
       <div class="mon-card">
         <div class="mon-label">📐 PITCH</div>
         <div class="mon-val" id="m-pitch">— <span class="unit">°</span></div>
       </div>
       <div class="mon-card">
         <div class="mon-label">🔊 VOLUME</div>
         <div class="mon-val" id="m-vol">—</div>
       </div>
 
       <div class="mon-card">
         <div class="mon-label">🎵 TRACK</div>
         <div class="mon-val" id="m-trk" style="font-size:.8rem">—</div>
       </div>
       <div class="mon-card">
         <div class="mon-label">🎙 STATE</div>
         <div class="mon-val" id="m-state" style="font-size:.8rem">—</div>
       </div>
     </div>
 
     <div class="sbar" id="sbar-mon">
       <span><span class="dot"></span><span>LIVE</span></span>
       <span id="sbar-mon-time">—</span>
     </div>
   </div>
 
   <div class="foot">GOOSEBOT · UB CSE SHOWCASE</div>
 </div>
 
 <script>
 // ─── Track data ───
 const TRACKS = [
   "", "Nirvana", "Manchild", "Tainted Love", "Umbrella", "Rasputin",
   "Mission Impossible", "James Bond", "Sweet Dreams", "End of Beginning",
   "Hoobastank", "The Middle", "You Spin Me Right Round", "Free Bird",
   "Together Forever", "Dancing Queen", "Funky Town", "Fireflies",
   "What Is Love", "Bad To The Bone", "Break My Stride",
   "Where'd All The Time Go", "Thunderstruck", "Makin' It", "Crab Rave",
   "Macarena", "Lady", "Be Like A Woman"
 ];
 const HONKS = [
   "", "Honk", "Canada Goose 2", "Canada Goose Honk", "Honk 1",
   "Honk H1", "Honk H2", "Honk Sound", "Honk 1b", "Honk b",
   "Honk 1c", "Honk c", "Honk Sound b", "Honk d", "Honk e",
   "Honk Sound c", "Honk Take", "Car Honk"
 ];
 const SOUNDS = [
   "", "Apple Pay", "Lego", "Smoke Alarm", "Pan", "Freddy",
   "Bell", "Windows", "Bonk", "Snake", "Dial-up", "Discord", "Spring",
   "Golpe", "Slip Slip Skidoo", "BB Hello", "Wilhelm Scream", "R2-D2"
 ];
 
 let currentTab = 'honks';
 let volTimer = null;
 
 // ─── Routing ───
 function goto(page) { location.hash = page === 'home' ? '' : page; }
 function applyRoute() {
   const hash = (location.hash.replace('#', '') || 'home');
   document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
   const target = document.getElementById('page-' + hash);
   (target || document.getElementById('page-home')).classList.add('active');
   window.scrollTo(0, 0);
 }
 window.addEventListener('hashchange', applyRoute);
 applyRoute();
 
 // ─── Build track list ───
 const trackList = document.getElementById('track-list');
 for (let i = 1; i < TRACKS.length; i++) {
   const div = document.createElement('div');
   div.className = 'li';
   div.id = 'tr-' + i;
   div.innerHTML = `<span class="li-num">${String(i).padStart(2,'0')}</span>` +
                   `<span class="li-name">${TRACKS[i]}</span>` +
                   `<span class="li-icon">▶</span>`;
   div.onclick = () => playMusic(i);
   trackList.appendChild(div);
 }
 
 // ─── Soundboard tabs ───
 function setTab(tab) {
   currentTab = tab;
   document.getElementById('tab-honks').classList.toggle('active', tab === 'honks');
   document.getElementById('tab-sounds').classList.toggle('active', tab === 'sounds');
   document.getElementById('random-btn').textContent = tab === 'honks' ? '🎲 RANDOM HONK' : '🎲 RANDOM SOUND';
   document.getElementById('sec-label').textContent = tab === 'honks' ? '🪿 ALL HONKS' : '🎚️ ALL SOUNDS';
   buildSoundGrid();
 }
 
 function buildSoundGrid() {
   const grid = document.getElementById('sound-grid');
   const data = currentTab === 'honks' ? HONKS : SOUNDS;
   grid.innerHTML = '';
   for (let i = 1; i < data.length; i++) {
     const div = document.createElement('div');
     div.className = 'li';
     div.innerHTML = `<span class="li-num">${String(i).padStart(2,'0')}</span>` +
                     `<span class="li-name">${data[i]}</span>`;
     div.onclick = () => playSound(i);
     grid.appendChild(div);
   }
 }
 buildSoundGrid();
 
 // ─── Commands ───
 function cmd(action, params) {
   let body = 'action=' + action;
   if (params) for (const k in params) body += '&' + k + '=' + encodeURIComponent(params[k]);
   return fetch('/cmd', {
     method: 'POST',
     headers: {'Content-Type':'application/x-www-form-urlencoded'},
     body: body
   });
 }
 function playMusic(id) { cmd('music', {id}); }
 function playSound(id) { cmd(currentTab === 'honks' ? 'honk' : 'sound', {id}); }
 function playRandom()  { cmd(currentTab === 'honks' ? 'honkrnd' : 'soundrnd'); }
 function updateVolDisp(v) {
   document.getElementById('vol-val-m').textContent = v;
   document.getElementById('vol-val-s').textContent = v;
 }
 function setVol(v) { cmd('volume', {v}); }
 
 // ─── Status polling ───
 function poll() {
   fetch('/status').then(r => r.json()).then(s => {
     updateUI(s);
     setSbar('ok', 'LIVE');
   }).catch(() => setSbar('err', 'NO SIGNAL'));
 }
 
 function setSbar(state, txt) {
   document.querySelectorAll('.sbar').forEach(sb => {
     sb.className = 'sbar ' + state;
     const t = sb.querySelector('span span:last-child');
     if (t && t.id !== 'sbar-mon-time' && !t.textContent.match(/\d/)) t.textContent = txt;
   });
   const homeTxt = document.getElementById('sb-home-txt');
   if (homeTxt) homeTxt.textContent = txt;
 }
 
 function tempClass(t) { return t < 50 ? 'good' : t < 70 ? 'warn' : 'hot'; }
 function battClass(b) { return b > 40 ? 'good' : b > 20 ? 'warn' : 'hot'; }
 
 function updateUI(s) {
   // Now Playing
   const np = document.getElementById('np-music');
   const npT = document.getElementById('np-title');
   const npS = document.getElementById('np-sub');
   if (s.playing && s.track > 0) {
     np.classList.remove('idle');
     npT.textContent = TRACKS[s.track] || 'Track ' + s.track;
     npS.textContent = `track ${String(s.track).padStart(2,'0')} of ${TRACKS.length-1}`;
   } else if (s.track > 0) {
     np.classList.add('idle');
     npT.textContent = TRACKS[s.track] || 'Track ' + s.track;
     npS.textContent = 'paused · tap ▶ to resume';
   } else {
     np.classList.add('idle');
     npT.textContent = '— NOTHING —';
     npS.textContent = 'tap a track to start';
   }
 
   // Highlight playing track
   for (let i = 1; i < TRACKS.length; i++) {
     const el = document.getElementById('tr-' + i);
     if (el) el.classList.toggle('playing', s.playing && s.track === i);
   }
 
   // Volume sync (only if user isn't dragging)
   if (s.vol !== undefined) {
     const slM = document.getElementById('vol-slider-m');
     const slS = document.getElementById('vol-slider-s');
     if (document.activeElement !== slM && document.activeElement !== slS) {
       slM.value = s.vol;
       slS.value = s.vol;
       document.getElementById('vol-val-m').textContent = s.vol;
       document.getElementById('vol-val-s').textContent = s.vol;
     }
   }
 
   // Monitor cards
   if (s.batt !== undefined) {
     const battEl = document.getElementById('m-batt');
     const fill = document.getElementById('m-batt-fill');
     battEl.innerHTML = s.batt + ' <span class="unit">%</span>';
     fill.style.width = s.batt + '%';
     battEl.className = 'mon-val ' + battClass(s.batt);
     fill.className = 'batt-fill ' + (battClass(s.batt) === 'good' ? '' : battClass(s.batt));
   }
   if (s.volts !== undefined) {
     document.getElementById('m-volts').innerHTML = s.volts.toFixed(1) + ' <span class="unit">V</span>';
   }
   if (s.velL !== undefined) document.getElementById('m-rpm-l').textContent = s.velL;
   if (s.velR !== undefined) document.getElementById('m-rpm-r').textContent = s.velR;
   if (s.tempL !== undefined) {
     const el = document.getElementById('m-temp-l');
     el.innerHTML = s.tempL + ' <span class="unit">°C</span>';
     el.className = 'mon-val ' + tempClass(s.tempL);
   }
   if (s.tempR !== undefined) {
     const el = document.getElementById('m-temp-r');
     el.innerHTML = s.tempR + ' <span class="unit">°C</span>';
     el.className = 'mon-val ' + tempClass(s.tempR);
   }
   if (s.pitch !== undefined) {
     document.getElementById('m-pitch').innerHTML = s.pitch.toFixed(1) + ' <span class="unit">°</span>';
   }
   if (s.vol !== undefined)   document.getElementById('m-vol').textContent = s.vol;
   if (s.track !== undefined) {
     document.getElementById('m-trk').textContent = s.track > 0
       ? (TRACKS[s.track] || 'T' + s.track) : 'idle';
   }
 
   let st = [];
   if (s.dj)      st.push('DJ');
   if (s.hopper)  st.push('HOPPER');
   if (s.playing) st.push('PLAY');
   document.getElementById('m-state').textContent = st.length ? st.join(' · ') : 'idle';
 
   // Death banner
   const wrap = document.getElementById('dead-banner-wrap');
   if (s.dead) {
     if (!wrap.firstChild) wrap.innerHTML = '<div class="dead-banner">⚠ GOOSE TIPPED OVER ⚠</div>';
   } else {
     wrap.innerHTML = '';
   }
 
   const tEl = document.getElementById('sbar-mon-time');
   if (tEl) tEl.textContent = new Date().toLocaleTimeString();
 }
 
 setInterval(poll, 1000);
 poll();
 </script>
 </body>
 </html>
 )HTML";
 // ═════════════════════════════════════════════════════════════════════════════
 
 
 // ── Helpers ─────────────────────────────────────────────────────────────────
 void sendToMega(const String& cmd) {
   MEGA_SERIAL.println(cmd);
   Serial.print("[→ MEGA] ");
   Serial.println(cmd);
 }
 
 String buildStatusJSON() {
   String j = "{";
   j += "\"track\":"   + String(currentTrack) + ",";
   j += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
   j += "\"vol\":"     + String(currentVolume) + ",";
   j += "\"velL\":"    + String(velL) + ",";
   j += "\"velR\":"    + String(velR) + ",";
   j += "\"tempL\":"   + String(tempL) + ",";
   j += "\"tempR\":"   + String(tempR) + ",";
   j += "\"volts\":"   + String(voltage, 1) + ",";
   j += "\"batt\":"    + String(battPct) + ",";
   j += "\"dj\":"      + String(djMode ? "true" : "false") + ",";
   j += "\"hopper\":"  + String(hopperOpen ? "true" : "false") + ",";
   j += "\"pitch\":"   + String(pitch, 1) + ",";
   j += "\"dead\":"    + String(isDead ? "true" : "false");
   j += "}";
   return j;
 }
 
 void parseMegaLine(const String& line) {
   if (line.startsWith("TRACK:")) {
     currentTrack = line.substring(6).toInt();
     isPlaying = true;
     Serial.print("[← MEGA] Track ");
     Serial.println(currentTrack);
   }
   else if (line == "STOPPED" || line == "PAUSED") {
     isPlaying = false;
     Serial.print("[← MEGA] ");
     Serial.println(line);
   }
   else if (line.startsWith("STATS:")) {
     // CSV: velL,velR,tempL,tempR,volts,batt,vol,trk,dj,hop,pitch,dead,playing
     String data = line.substring(6);
     String fields[13];
     int idx = 0, start = 0;
     for (int i = 0; i <= (int)data.length() && idx < 13; i++) {
       if (i == (int)data.length() || data[i] == ',') {
         fields[idx++] = data.substring(start, i);
         start = i + 1;
       }
     }
     if (idx >= 13) {
       velL          = fields[0].toInt();
       velR          = fields[1].toInt();
       tempL         = fields[2].toInt();
       tempR         = fields[3].toInt();
       voltage       = fields[4].toFloat();
       battPct       = fields[5].toInt();
       currentVolume = fields[6].toInt();
       currentTrack  = fields[7].toInt();
       djMode        = fields[8].toInt() != 0;
       hopperOpen    = fields[9].toInt() != 0;
       pitch         = fields[10].toFloat();
       isDead        = fields[11].toInt() != 0;
       isPlaying     = fields[12].toInt() != 0;
     }
   }
 }
 
 
 // ── Setup ───────────────────────────────────────────────────────────────────
 void setup() {
   Serial.begin(115200);
   delay(2500);
   Serial.println("\n=== GooseBot Web Control ===");
 
   MEGA_SERIAL.begin(9600);
   Serial.println("Mega serial ready (9600)");
 
   WiFi.softAP(SSID, PASSWORD);
   Serial.print("AP IP: ");
   Serial.println(WiFi.softAPIP());
 
   // Serve SPA
   server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
     req->send_P(200, "text/html", PAGE);
   });
 
   // Status JSON
   server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req) {
     req->send(200, "application/json", buildStatusJSON());
   });
 
   // Commands
   server.on("/cmd", HTTP_POST, [](AsyncWebServerRequest* req) {
     if (!req->hasParam("action", true)) {
       req->send(400, "text/plain", "missing action");
       return;
     }
     String a = req->getParam("action", true)->value();
 
     if (a == "music" && req->hasParam("id", true)) {
       int id = req->getParam("id", true)->value().toInt();
       if (id >= 1 && id <= 27) {
         char buf[16]; snprintf(buf, sizeof(buf), "MUSIC:%02d", id);
         sendToMega(buf);
       }
     }
     else if (a == "honk" && req->hasParam("id", true)) {
       int id = req->getParam("id", true)->value().toInt();
       char buf[16]; snprintf(buf, sizeof(buf), "HONK:%02d", id);
       sendToMega(buf);
     }
     else if (a == "honkrnd")  sendToMega("HONK:RND");
     else if (a == "sound" && req->hasParam("id", true)) {
       int id = req->getParam("id", true)->value().toInt();
       char buf[16]; snprintf(buf, sizeof(buf), "SOUND:%02d", id);
       sendToMega(buf);
     }
     else if (a == "soundrnd") sendToMega("SOUND:RND");
     else if (a == "next")     sendToMega("NEXT");
     else if (a == "prev")     sendToMega("PREV");
     else if (a == "pause")    sendToMega("PAUSE");
     else if (a == "resume")   sendToMega("RESUME");
     else if (a == "stop")     sendToMega("STOP");
     else if (a == "volup")    sendToMega("VOL:UP");
     else if (a == "voldown")  sendToMega("VOL:DOWN");
     else if (a == "volume" && req->hasParam("v", true)) {
       int v = req->getParam("v", true)->value().toInt();
       char buf[16]; snprintf(buf, sizeof(buf), "VOL:%d", v);
       sendToMega(buf);
     }
 
     req->send(200, "text/plain", "OK");
   });
 
   server.begin();
   Serial.println("Web server started");
   Serial.println("Connect to GooseBot WiFi → http://192.168.4.1");
   Serial.println("============================");
 }
 
 
 // ── Loop: process incoming serial from Mega ────────────────────────────────
 void loop() {
   static String megaLine = "";
   while (MEGA_SERIAL.available()) {
     char c = (char)MEGA_SERIAL.read();
     if (c == '\n') {
       megaLine.trim();
       if (megaLine.length() > 0) parseMegaLine(megaLine);
       megaLine = "";
     } else if (c != '\r' && megaLine.length() < 128) {
       megaLine += c;
     }
   }
 }