/*
 * Built-in example designs for the playground page. Each must stay inside
 * the HTML whitelist (docs/html-path.md): div/p/span/label/small/button/
 * checkbox/radio/toggle/gauge/knob/trend/meter/svg, the CSS subset
 * (flex, gradients, borders, shadows, absolute positioning), no JS, no
 * external resources.
 */
window.IMPRINT_DESIGNS = {

"Instrument panel": `<!DOCTYPE html>
<html>
<head>
<style>
  :root { --panel: #101826; --edge: #22344e; --ink: #7fd4ff; --dim: #5c7590; }
  body { background: #0a0e16; margin: 0; font-family: sans-serif; }
  .console {
    display: flex; flex-direction: column; gap: 14px;
    width: 320px; height: 240px; padding: 16px;
    background: linear-gradient(180deg, #0e1522, #0a0e16);
    box-sizing: border-box;
  }
  .row { display: flex; gap: 12px; align-items: center; }
  .dials { display: flex; gap: 10px; justify-content: space-between; }
  .cell {
    background: var(--panel); border: 1px solid var(--edge);
    border-radius: 8px; padding: 10px;
    display: flex; flex-direction: column; align-items: center; gap: 6px;
  }
  .title { color: var(--dim); font-size: 10px; letter-spacing: 3px; }
  h1 { color: var(--ink); font-size: 18px; margin: 0; letter-spacing: 2px; }
  small { color: var(--dim); font-size: 9px; letter-spacing: 1px; }
</style>
</head>
<body>
<div class="console">
  <div class="row">
    <h1>SIGNAL LAB</h1>
    <small>CH 03 · LOCKED</small>
  </div>
  <div class="dials">
    <div class="cell"><gauge id="g" value="62"></gauge><span class="title">GAIN</span></div>
    <div class="cell"><gauge id="g2" value="38"></gauge><span class="title">TUNE</span></div>
    <div class="cell"><gauge id="g3" value="81"></gauge><span class="title">BIAS</span></div>
  </div>
  <div class="cell" style="width: 100%">
    <trend value="30"></trend><span class="title">SPECTRUM</span>
  </div>
  <div class="row">
    <meter value="74" style="width: 180px"></meter>
    <small>74%</small>
    <toggle checked="1"></toggle>
  </div>
</div>
</body>
</html>`,

"Cards": `<!DOCTYPE html>
<html>
<head>
<style>
  body { margin: 0; background: #0c111c; font-family: sans-serif; }
  .board {
    display: flex; flex-wrap: wrap; gap: 12px; padding: 16px;
    width: 320px; height: 240px; box-sizing: border-box;
  }
  .card {
    width: 136px; height: 98px; border-radius: 10px;
    padding: 12px; box-sizing: border-box;
    display: flex; flex-direction: column; gap: 6px;
    background: linear-gradient(160deg, #1b2a44, #101826);
    border: 1px solid #2a3c5c;
    box-shadow: 0 8px 18px rgba(0,0,0,0.45);
  }
  .card.warm { background: linear-gradient(160deg, #402a1e, #201410); border-color: #6b4a30; }
  .k { color: #6d7f98; font-size: 10px; letter-spacing: 2px; }
  .v { color: #eaf2fb; font-size: 22px; font-weight: bold; }
  .d { color: #57c46a; font-size: 10px; }
  .card.warm .d { color: #f0b243; }
</style>
</head>
<body>
<div class="board">
  <div class="card">
    <span class="k">UPTIME</span>
    <span class="v">99.98%</span>
    <span class="d">+0.02 this week</span>
  </div>
  <div class="card warm">
    <span class="k">LATENCY</span>
    <span class="v">42ms</span>
    <span class="d">p99 · watch</span>
  </div>
  <div class="card warm">
    <span class="k">QUEUE</span>
    <span class="v">1 208</span>
    <span class="d">draining</span>
  </div>
  <div class="card">
    <span class="k">NODES</span>
    <span class="v">18</span>
    <span class="d">all nominal</span>
  </div>
</div>
</body>
</html>`,

"Vector art": `<!DOCTYPE html>
<html>
<head>
<style>
  body { margin: 0; background: radial-gradient(300px 200px at 50% 40%, #14203a, #090d14); }
  .stage { width: 320px; height: 240px; position: relative; }
  .frame {
    position: absolute; left: 24px; top: 24px; width: 272px; height: 192px;
    border: 1px solid #2c4260; border-radius: 12px;
    background: linear-gradient(180deg, rgba(20,32,58,0.5), rgba(9,13,20,0));
    box-shadow: 0 10px 30px rgba(0,0,0,0.5);
  }
  p { position: absolute; left: 40px; top: 176px; color: #7fd4ff;
      font-size: 13px; letter-spacing: 4px; margin: 0; text-shadow: 0 2px 6px #000; }
  small { position: absolute; left: 40px; top: 196px; color: #55688a; font-size: 9px; letter-spacing: 2px; }
</style>
</head>
<body>
<div class="stage">
  <div class="frame">
    <svg width="272" height="192" viewBox="0 0 272 192">
      <circle cx="60" cy="60" r="28" fill="none" stroke="#f0b243" stroke-width="3"/>
      <ellipse cx="150" cy="70" rx="40" ry="18" fill="none" stroke="#4fd6e0" stroke-width="2"/>
      <line x1="30" y1="150" x2="240" y2="150" stroke="#33475f" stroke-width="1"/>
      <polyline points="30,130 90,90 140,110 200,60 244,80"
                fill="none" stroke="#7fd4ff" stroke-width="2"/>
      <rect x="190" y="30" width="50" height="26" fill="#16233a" stroke="#2c4260"/>
      <circle cx="96" cy="60" r="4" fill="#e26d5a"/>
    </svg>
  </div>
  <p>VECTOR PATH</p>
  <small>SVG SUBSET · 4 ELEMENTS</small>
</div>
</body>
</html>`,

"Controls": `<!DOCTYPE html>
<html>
<head>
<style>
  body { margin: 0; background: #0c111c; font-family: sans-serif; }
  .panel {
    display: flex; flex-direction: column; gap: 12px;
    width: 320px; height: 240px; padding: 18px; box-sizing: border-box;
    background: linear-gradient(180deg, #111a2a, #0b101a);
  }
  .row { display: flex; gap: 14px; align-items: center; }
  label { color: #93a7c4; font-size: 12px; width: 90px; }
  .group {
    border: 1px solid #22344e; border-radius: 8px; padding: 10px;
    display: flex; gap: 16px;
  }
  span.title { color: #5c7590; font-size: 10px; letter-spacing: 3px; }
</style>
</head>
<body>
<div class="panel">
  <span class="title">SETTINGS</span>
  <div class="row"><label>Slice size</label><slider value="35" min="0" max="100"></slider></div>
  <div class="row"><label>Threaded</label><toggle checked="1"></toggle></div>
  <div class="row"><label>Auto scale</label><checkbox></checkbox></div>
  <div class="group">
    <radio name="algo" checked="1"></radio><label>Fast</label>
    <radio name="algo"></radio><label>Precise</label>
  </div>
  <div class="row">
    <button>APPLY</button>
    <button>RESET</button>
  </div>
</div>
</body>
</html>`
};
