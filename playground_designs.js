/*
 * Built-in example designs for the playground page. Each must stay inside
 * the HTML whitelist (docs/html-path.md): div/p/span/label/small/button/
 * checkbox/radio/toggle/gauge/knob/trend/meter/svg, the CSS subset
 * (flex, gradients, borders, shadows, pseudo decoration boxes, absolute
 * positioning), no JS, no external resources. All samples are authored
 * against the fixed 800x640 playground buffer.
 */
window.IMPRINT_DESIGNS = {

"Power amplifier": `<!DOCTYPE html>
<html>
<head>
<style>
  body {
    width: 800px; height: 640px; margin: 0; box-sizing: border-box;
    padding: 26px;
    display: flex; flex-direction: column;
    background: linear-gradient(180deg, #1c1208 0%, #0b0704 100%);
    font-family: sans-serif;
  }
  .face {
    flex: 1;
    display: flex; flex-direction: column; gap: 20px;
    padding: 26px;
    border: 1px solid #6b4a2a; border-radius: 18px;
    background:
      repeating-linear-gradient(90deg, rgba(255,225,170,0.04) 0 1px, rgba(0,0,0,0.05) 1px 3px),
      linear-gradient(180deg, #382718 0%, #22150a 55%, #150d05 100%);
    box-shadow: inset 0 1px 0 rgba(255,220,160,0.16), 0 24px 60px rgba(0,0,0,0.55);
  }
  .head { display: flex; align-items: center; justify-content: space-between; }
  .brand { display: flex; align-items: center; gap: 14px; }
  .mark {
    width: 40px; height: 40px; border-radius: 10px;
    border: 1px solid #8a5a26;
    background: linear-gradient(180deg, #59391a 0%, #2b1a0b 100%);
    box-shadow: inset 0 1px 0 rgba(255,210,140,0.25), 0 0 18px rgba(255,148,28,0.25);
  }
  h1 { margin: 0; font-size: 24px; font-weight: bold; letter-spacing: 3px; color: #f3e3c2; }
  .sub { font-size: 11px; letter-spacing: 3px; color: #a98c5e; }
  .lamprow { display: flex; align-items: center; gap: 16px; }
  .lamp { display: flex; align-items: center; gap: 6px; }
  .dot { width: 10px; height: 10px; border-radius: 50%;
         background: #241708; border: 1px solid #6b4a2a; }
  .dot.on { background: #ffb143; border: 1px solid #ffb143;
            box-shadow: 0 0 12px rgba(255,177,67,0.8); }
  .lamp span { font-size: 10px; letter-spacing: 2px; color: #c7ab7c; }

  .deck { flex: 1; display: flex; gap: 24px; }
  .knobcell {
    width: 190px; padding: 16px;
    display: flex; flex-direction: column; align-items: center; gap: 12px;
    border: 1px solid #55381c; border-radius: 14px;
    background: linear-gradient(180deg, rgba(0,0,0,0.25) 0%, rgba(0,0,0,0.05) 100%);
  }
  .knob2 {
    position: relative; width: 150px; height: 150px; border-radius: 50%;
    border: 2px solid #6f5533;
    background: radial-gradient(circle at 36% 30%, #c9b18a 0%, #2c1d0e 74%);
    box-shadow: inset 0 -8px 14px rgba(0,0,0,0.6), inset 0 3px 6px rgba(255,235,190,0.2), 0 12px 26px rgba(0,0,0,0.55);
  }
  .knob2::before {
    content: ""; position: absolute;
    left: 26px; top: 12px; width: 60px; height: 34px; border-radius: 50%;
    background: linear-gradient(180deg, rgba(255,240,205,0.35) 0%, rgba(255,240,205,0.08) 60%, rgba(255,240,205,0) 100%);
  }
  .knob2::after {
    content: ""; position: absolute;
    left: 50%; top: 14px; width: 5px; height: 44px; border-radius: 3px;
    background: #ffb143;
    transform-origin: 50% 100%;
    transform: rotate(-58deg);
  }
  .knoblab { font-size: 12px; font-weight: bold; letter-spacing: 3px; color: #d9bd8c; }
  .knobval { font-size: 10px; letter-spacing: 2px; color: #96805c; }

  .vu {
    flex: 1;
    display: flex; flex-direction: column; gap: 12px;
    padding: 16px;
    border: 1px solid #55381c; border-radius: 14px;
    background: rgba(0,0,0,0.22);
  }
  .vudisp {
    position: relative; flex: 1; border-radius: 10px;
    border: 1px solid #a16e32;
    background:
      repeating-linear-gradient(90deg, rgba(90,45,10,0.35) 0 2px, rgba(0,0,0,0) 2px 34px),
      linear-gradient(180deg, #f0b856 0%, #f8d689 48%, #b06f24 100%);
    box-shadow: inset 0 0 30px rgba(60,25,0,0.45);
  }
  .vudisp::before {
    content: ""; position: absolute;
    left: 50%; bottom: 0; width: 4px; height: 78%;
    background: #571d06; border-radius: 2px;
    transform-origin: 50% 100%;
    transform: rotate(32deg);
  }
  .vuscale { display: flex; justify-content: space-between; padding: 0 6px; }
  .vuscale span { font-size: 11px; font-weight: bold; color: #4a290b; }
  .vutitle { display: flex; justify-content: space-between; align-items: center; }
  .vutitle b { font-size: 14px; letter-spacing: 4px; color: #e8cf9f; }
  .vutitle small { font-size: 10px; letter-spacing: 2px; color: #96805c; }

  .foot { display: flex; align-items: center; justify-content: space-between; }
  .foot .mrow { display: flex; align-items: center; gap: 14px; }
  .foot .mrow meter { width: 170px; }
  .foot .mrow span { font-size: 11px; letter-spacing: 2px; color: #c7ab7c; }
  .plate {
    padding: 8px 18px;
    border: 1px solid #55381c; border-radius: 6px;
    background: rgba(0,0,0,0.3);
  }
  .plate span { font-size: 10px; letter-spacing: 4px; color: #96805c; }
</style>
</head>
<body>
<div class="face">
  <div class="head">
    <div class="brand">
      <div class="mark"></div>
      <div>
        <h1>AURORA A-80</h1>
        <div class="sub">INTEGRATED AMPLIFIER · CLASS A</div>
      </div>
    </div>
    <div class="lamprow">
      <div class="lamp"><div class="dot on"></div><span>POWER</span></div>
      <div class="lamp"><div class="dot on"></div><span>SIGNAL</span></div>
      <div class="lamp"><div class="dot"></div><span>CLIP</span></div>
    </div>
  </div>

  <div class="deck">
    <div class="knobcell">
      <div class="knob2"></div>
      <div class="knoblab">VOLUME</div>
      <div class="knobval">-18.5 dB</div>
    </div>
    <div class="knobcell">
      <div class="knob2"></div>
      <div class="knoblab">BIAS</div>
      <div class="knobval">26 mV</div>
    </div>
    <div class="vu">
      <div class="vutitle"><b>VU / L</b><small>0 VU = +4 dBu</small></div>
      <div class="vudisp"></div>
      <div class="vuscale">
        <span>-20</span><span>-10</span><span>-7</span><span>-5</span><span>-3</span><span>-1</span><span>0</span><span>+3</span>
      </div>
    </div>
  </div>

  <div class="foot">
    <div class="mrow">
      <span>PSU</span><meter value="82"></meter>
      <span>HEAT</span><meter value="41"></meter>
    </div>
    <div class="plate"><span>AURORA AMPLIFICATION · EST. 1979</span></div>
  </div>
</div>
</body>
</html>`,

"Glass dashboard": `<!DOCTYPE html>
<html>
<head>
<style>
  body {
    width: 800px; height: 640px; margin: 0; box-sizing: border-box;
    padding: 26px;
    display: flex; flex-direction: column; gap: 18px;
    background:
      radial-gradient(circle at 50% 0%, #21365e 0%, rgba(10,16,32,0) 70%),
      linear-gradient(180deg, #0c1424 0%, #070b14 100%);
    font-family: sans-serif;
  }
  .head { display: flex; align-items: center; justify-content: space-between; }
  .kicker { font-size: 12px; letter-spacing: 5px; color: #9fc0ff; }
  .live {
    display: flex; align-items: center; gap: 8px;
    padding: 6px 14px; border-radius: 20px;
    border: 1px solid rgba(255,255,255,0.14);
    background: rgba(255,255,255,0.06);
  }
  .live .pulse { width: 9px; height: 9px; border-radius: 50%;
                 background: #6fe08a; box-shadow: 0 0 10px rgba(111,224,138,0.9); }
  .live span { font-size: 11px; letter-spacing: 2px; color: #cfe0ff; }

  .row { display: flex; gap: 18px; }
  .card {
    flex: 1;
    display: flex; flex-direction: column; gap: 8px;
    padding: 20px;
    border: 1px solid rgba(255,255,255,0.12); border-radius: 16px;
    background: rgba(255,255,255,0.055);
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.14), 0 20px 44px rgba(0,0,0,0.38);
  }
  .k { font-size: 11px; letter-spacing: 3px; color: #8ea6cf; }
  .v { font-size: 34px; font-weight: bold; color: #f2f7ff; }
  .d { font-size: 11px; letter-spacing: 1px; color: #6fe08a; }
  .d.warn { color: #ffc66a; }
  .span2 { flex: 2; }

  .panel {
    flex: 1;
    display: flex; gap: 18px;
    padding: 20px;
    border: 1px solid rgba(255,255,255,0.12); border-radius: 16px;
    background: rgba(255,255,255,0.045);
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.12), 0 20px 44px rgba(0,0,0,0.38);
  }
  .gaugecard {
    width: 220px;
    display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 10px;
  }
  .big { font-size: 15px; letter-spacing: 4px; color: #9fc0ff; }
  .bigv { font-size: 30px; font-weight: bold; color: #f2f7ff; }
  .trendcard {
    flex: 1;
    display: flex; flex-direction: column; gap: 12px; justify-content: center;
  }
  .spark {
    width: 100%; height: 150px;
    border: 1px solid rgba(255,255,255,0.12); border-radius: 10px;
    background: rgba(10,16,32,0.55);
  }
  .mrow { display: flex; align-items: center; gap: 12px; }
  .mrow meter { flex: 1; }
  .mrow span { font-size: 11px; letter-spacing: 2px; color: #8ea6cf; width: 64px; }
</style>
</head>
<body>
  <div class="head">
    <div class="kicker">MISSION CONTROL · GLASS PANEL</div>
    <div class="live"><div class="pulse"></div><span>LIVE</span></div>
  </div>

  <div class="row">
    <div class="card">
      <span class="k">THROUGHPUT</span>
      <span class="v">4.2 Gb/s</span>
      <span class="d">+12% vs last hour</span>
    </div>
    <div class="card">
      <span class="k">P99 LATENCY</span>
      <span class="v">38 ms</span>
      <span class="d warn">watch · was 21 ms</span>
    </div>
    <div class="card">
      <span class="k">ERROR RATE</span>
      <span class="v">0.03%</span>
      <span class="d">nominal</span>
    </div>
  </div>

  <div class="panel">
    <div class="gaugecard">
      <gauge value="68"></gauge>
      <div class="big">BUFFER</div>
      <div class="bigv">68%</div>
    </div>
    <div class="trendcard">
      <svg class="spark" viewBox="0 0 500 150">
        <polyline points="0,110 45,95 90,102 135,70 180,84 225,52 270,66 315,38 360,58 405,30 450,44 500,24"
                  fill="none" stroke="#6fe08a" stroke-width="2.5" opacity="0.9"/>
        <line x1="0" y1="120" x2="500" y2="120" stroke="rgba(255,255,255,0.15)" stroke-width="1"/>
      </svg>
      <div class="mrow"><span>ingest</span><meter value="72"></meter></div>
      <div class="mrow"><span>render</span><meter value="55"></meter></div>
      <div class="mrow"><span>drain</span><meter value="31"></meter></div>
    </div>
  </div>
</body>
</html>`,

"Dial cluster": `<!DOCTYPE html>
<html>
<head>
<style>
  body {
    width: 800px; height: 640px; margin: 0; box-sizing: border-box;
    padding: 26px;
    display: flex; gap: 26px;
    background: linear-gradient(180deg, #10181e 0%, #080d10 100%);
    font-family: sans-serif;
  }
  .dialbay {
    width: 400px;
    display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 14px;
    border: 1px solid #2c444d; border-radius: 18px;
    background: linear-gradient(180deg, #131f24 0%, #0a1114 100%);
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.06), 0 20px 50px rgba(0,0,0,0.45);
  }
  .dial { position: relative; width: 300px; height: 300px; }
  .ring {
    position: absolute; left: 0; right: 0; top: 0; bottom: 0;
    border-radius: 50%;
    background: conic-gradient(from 234deg, #35d7ff 0deg, #35d7ff 100deg, #17323b 100deg, #17323b 360deg);
    box-shadow: 0 0 30px rgba(53,215,255,0.18);
  }
  .face2 {
    position: absolute; left: 22px; right: 22px; top: 22px; bottom: 22px;
    border-radius: 50%;
    background: radial-gradient(circle at 38% 28%, #27505e 0%, #0a1418 65%);
    box-shadow: inset 0 0 50px rgba(0,0,0,0.55), 0 14px 34px rgba(0,0,0,0.5);
  }
  .face2::before {
    content: ""; position: absolute;
    left: 40px; top: 16px; width: 150px; height: 70px; border-radius: 50%;
    background: linear-gradient(180deg, rgba(255,255,255,0.14) 0%, rgba(255,255,255,0.04) 60%, rgba(255,255,255,0) 100%);
  }
  .dial::after {
    content: ""; position: absolute;
    left: 50%; top: 28px; width: 5px; height: 122px; border-radius: 3px;
    background: #7fe3ff;
    transform-origin: 50% 100%;
    transform: rotate(42deg);
  }
  .hub {
    position: absolute; left: 50%; top: 50%;
    width: 18px; height: 18px; border-radius: 50%;
    background: radial-gradient(circle at 40% 35%, #3d6d7d 0%, #0c171b 75%);
    border: 1px solid #3d5862;
    transform: translate(-50%, -50%);
  }
  .readout { font-size: 40px; font-weight: bold; color: #f2fbfd; letter-spacing: 2px; }
  .readlab { font-size: 11px; letter-spacing: 4px; color: #5fb9d1; }

  .stack {
    flex: 1;
    display: flex; flex-direction: column; gap: 16px; justify-content: center;
  }
  .mod {
    padding: 18px;
    border: 1px solid #24363e; border-radius: 14px;
    background: linear-gradient(180deg, #101a20 0%, #0a1014 100%);
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.05);
    display: flex; flex-direction: column; gap: 10px;
  }
  .mod .t { font-size: 11px; letter-spacing: 3px; color: #7d95a0; }
  .mod .r { display: flex; align-items: center; gap: 12px; }
  .mod .r span { font-size: 12px; color: #a9bcc4; width: 72px; }
  .mod .r slider { flex: 1; }
  .mod .val { font-size: 14px; font-weight: bold; color: #35d7ff; }
  .switchrow { display: flex; align-items: center; gap: 18px; }
  .switchrow span { font-size: 12px; color: #a9bcc4; }
  .chip {
    padding: 6px 14px; border: 1px solid #2c444d; border-radius: 6px;
    background: #0d181c;
  }
  .chip span { font-size: 10px; letter-spacing: 3px; color: #7d95a0; }
</style>
</head>
<body>
  <div class="dialbay">
    <div class="dial">
      <div class="ring"></div>
      <div class="face2"></div>
      <div class="hub"></div>
    </div>
    <div class="readlab">REACTOR OUTPUT</div>
    <div class="readout">62%</div>
  </div>

  <div class="stack">
    <div class="mod">
      <div class="t">COOLANT MIX</div>
      <div class="r"><span>primary</span><slider value="64"></slider><span class="val">64%</span></div>
      <div class="r"><span>reserve</span><slider value="22"></slider><span class="val">22%</span></div>
    </div>
    <div class="mod">
      <div class="t">SYSTEMS</div>
      <div class="switchrow"><toggle checked="1"></toggle><span>magnetic containment</span></div>
      <div class="switchrow"><toggle checked="1"></toggle><span>plasma recirculation</span></div>
      <div class="switchrow"><toggle></toggle><span>auxiliary burn</span></div>
    </div>
    <div class="mod">
      <div class="t">STATUS</div>
      <div class="r"><checkbox checked="1"></checkbox><span>telemetry downlink</span></div>
      <div class="r"><radio name="mode" checked="1"></radio><span>cruise</span>
        <radio name="mode"></radio><span>burst</span></div>
      <div class="chip"><span>NX-07 · DECK 2</span></div>
    </div>
  </div>
</body>
</html>`,

"Vector night": `<!DOCTYPE html>
<html>
<head>
<style>
  body {
    width: 800px; height: 640px; margin: 0;
    background: radial-gradient(circle at 50% 35%, #16233f 0%, #080d16 72%);
    font-family: sans-serif;
  }
  .stage { position: relative; width: 800px; height: 640px; }
  .frame {
    position: absolute; left: 46px; top: 40px; width: 708px; height: 480px;
    border: 1px solid #2c4260; border-radius: 14px;
    background: linear-gradient(180deg, rgba(23,36,66,0.55) 0%, rgba(9,13,20,0.1) 100%);
    box-shadow: 0 24px 60px rgba(0,0,0,0.55), inset 0 1px 0 rgba(160,200,255,0.12);
  }
  .frame svg { position: absolute; left: 0; top: 0; width: 708px; height: 480px; }
  p {
    position: absolute; left: 62px; top: 552px; margin: 0;
    font-size: 22px; letter-spacing: 8px; color: #9fc8ff;
    text-shadow: 0 3px 10px #000;
  }
  small {
    position: absolute; left: 62px; top: 588px;
    font-size: 12px; letter-spacing: 3px; color: #5a7196;
  }
</style>
</head>
<body>
<div class="stage">
  <div class="frame">
    <svg viewBox="0 0 708 480" width="708" height="480">
      <circle cx="560" cy="110" r="46" fill="none" stroke="#e8ecf5" stroke-width="2.5"/>
      <circle cx="545" cy="96" r="7" fill="#e8ecf5" opacity="0.5"/>
      <circle cx="580" cy="126" r="4" fill="#e8ecf5" opacity="0.4"/>
      <circle cx="120" cy="80" r="2.5" fill="#cfe0ff"/>
      <circle cx="220" cy="130" r="2" fill="#9fc8ff"/>
      <circle cx="330" cy="70" r="2.5" fill="#cfe0ff"/>
      <circle cx="420" cy="150" r="2" fill="#9fc8ff"/>
      <circle cx="90" cy="200" r="2" fill="#9fc8ff"/>
      <polygon points="0,480 140,300 260,410 380,270 520,430 620,340 708,480"
               fill="#0d1526" stroke="#2c4260" stroke-width="2"/>
      <polyline points="0,420 90,380 180,405 300,350 420,395 540,360 660,400 708,385"
                fill="none" stroke="#35d7ff" stroke-width="2" opacity="0.7"/>
      <path d="M 40 300 C 140 240, 260 330, 360 280 S 580 220, 670 260"
            fill="none" stroke="#57c8ff" stroke-width="2.5" opacity="0.85"/>
      <line x1="40" y1="440" x2="668" y2="440" stroke="#22344e" stroke-width="1.5"/>
    </svg>
  </div>
  <p>NIGHT SURVEY</p>
  <small>SVG SUBSET · PATH + POLYGON + POLYLINE · SOFTWARE STROKES</small>
</div>
</body>
</html>`
};
