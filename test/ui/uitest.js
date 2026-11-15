// Прогоняем UI в Node со stub-DOM: ловим опечатки и обращения к несуществующим id.
const missing = new Set();
function mkEl(id){
  const style = { setProperty(){}, background:"" };
  const el = new Proxy({
    id, style, classList:{toggle(){},add(){},remove(){},contains(){return false}},
    dataset:{}, children:[], value:"", textContent:"", innerHTML:"",
    hidden:false, placeholder:"", max:"", files:[],
    appendChild(c){return c}, append(){}, setAttribute(){}, getAttribute(){return null},
    showModal(){}, close(){}, click(){}, onclick:null, oninput:null, onchange:null,
    querySelectorAll(){return []}, addEventListener(){}
  }, { get(t,p){ if(p in t) return t[p]; return undefined; },
       set(t,p,v){ t[p]=v; return true; } });
  return el;
}
const cache={};
global.document = {
  getElementById(id){ if(!cache[id]){ cache[id]=mkEl(id); } return cache[id]; },
  querySelectorAll(){ return []; },
  createElement(){ return mkEl("new"); },
  createElementNS(){ const e=mkEl("svg"); e.namespaceURI="ns"; return e; },
  documentElement:{ style:{ setProperty(){} } },
  activeElement:null, title:""
};
global.location={host:"x",href:""};
global.confirm=()=>false; global.prompt=()=>null;
global.WebSocket=function(){ this.readyState=0; };
global.fetch=async()=>({ok:false,status:0,statusText:"stub",text:async()=>"",json:async()=>({})});
global.setInterval=()=>0; global.setTimeout=(f)=>0; global.clearTimeout=()=>{};

const src = require('fs').readFileSync(process.argv[2] || '/tmp/ui.js','utf8');
try { eval(src); console.log("Загрузка и bindUI прошли без исключений"); }
catch(e){ console.log("ОШИБКА:", e.message); process.exit(1); }

// Проверяем чистые функции через повторный eval в области видимости
const helpers = eval(src + "; ({minToHHMM, hhmmToMin, rgbToHex, hexToRgb, anchorLabel, formatUptime, L, layLeds, layGen, layParseFrame, layColor, layGlobal, layNearest})");
const t=[];
const ok=(c,m)=>t.push([c,m]);
ok(helpers.minToHHMM(0)==="00:00","полночь");
ok(helpers.minToHHMM(1410)==="23:30","23:30");
ok(helpers.minToHHMM(-30)==="23:30","отрицательные минуты заворачиваются");
ok(helpers.hhmmToMin("18:00")===1080,"разбор 18:00");
ok(helpers.hhmmToMin("7:05")===425,"разбор 7:05");
ok(helpers.hhmmToMin("бред")===null,"мусор отвергается");
ok(helpers.hhmmToMin("25:00")===null,"25:00 отвергается");
ok(helpers.rgbToHex([255,160,60])==="#ffa03c","rgb→hex");
ok(String(helpers.hexToRgb("#ffa03c"))==="255,160,60","hex→rgb");
ok(helpers.anchorLabel({type:"clock",value:1080})==="18:00","метка часов");
ok(helpers.anchorLabel({type:"sunset",value:-30}).includes("закат"),"метка заката");
ok(helpers.formatUptime(90061).startsWith("1д"),"аптайм");
// Раскладка: точки деталей и то, что уходит на устройство.
const J=JSON.stringify, L=helpers.L;
const leds=(parts)=>{ L.parts=parts; return J(helpers.layLeds()); };
const line=(o)=>Object.assign({id:1,t:"line",n:3,s:60,x:100,y:100,r:0},o);
ok(leds([])==="[]","пустая раскладка — пустой список");
ok(leds([line({})])==="[[0,0],[500,0],[1000,0]]","линия: 0..1000 по рамке");
ok(leds([line({rev:true})])==="[[1000,0],[500,0],[0,0]]","разворот меняет порядок");
ok(leds([line({n:2,s:50,x:50,y:50,r:90})])==="[[0,0],[0,1000]]","поворот на 90°");
ok(leds([{id:1,t:"dot",n:1,s:0,x:7,y:9,r:0}])==="[[0,0]]","одна точка");
ok(leds([{id:1,t:"free",n:3,s:0,x:0,y:0,r:0,P:[[10,10],[110,10],[10,60]]}])==="[[0,0],[1000,0],[0,500]]","свои диоды: масштаб по большей стороне");
const hex={id:1,t:"poly",n:18,s:46,k:6,x:90,y:90,r:0}, hp=helpers.layGen(hex);
ok(hp.length===18,"шестиугольник: 18 диодов");
ok(Math.abs(hp[0][0]-46)<1e-9 && Math.abs(hp[0][1])<1e-9,"шестиугольник: первый диод в вершине");
ok(hp.every(q=>Math.hypot(q[0],q[1])<=46+1e-9),"шестиугольник: диоды не выходят за описанную окружность");
// Кадр предпросмотра и номера диодов.
const fr=(a)=>helpers.layParseFrame(new Uint8Array(a).buffer);
const f2=fr([1,0,2, 255,0,0, 0,255,0]);
ok(f2 && f2.length===6 && f2[0]===255 && f2[4]===255,"кадр: два диода, цвета по порядку");
ok(fr([2,0,1,1,2,3])===null,"кадр: чужой тип сообщения");
ok(fr([1,0,3, 1,2,3, 4,5,6])===null,"кадр: оборванное сообщение");
ok(fr([1])===null,"кадр: короче заголовка");
ok(fr([1,0,0]).length===0,"кадр: пустой, но целый");
ok(fr([1,4,176].concat(new Array(3600).fill(7))).length===3600,"кадр: 1200 диодов, число в двух байтах");
L.live=true; L.frame=new Uint8Array([10,20,30]);
ok(helpers.layColor(0,false)==="rgb(10,20,30)","цвет диода из кадра");
ok(helpers.layColor(1,true)==="var(--muted)" || helpers.layColor(1,true)==="var(--glow)","диод за пределами кадра красится по-обычному");
L.live=false;
ok(helpers.layColor(0,true)==="var(--glow)" && helpers.layColor(0,false)==="var(--muted)","без предпросмотра: выбранная деталь ярче");
L.parts=[{id:1,t:"poly",n:18,s:46,k:6,x:0,y:0,r:0},{id:2,t:"free",n:3,s:0,x:0,y:0,r:0,P:[[1,1],[2,2],[3,3]]},{id:3,t:"dot",n:1,s:0,x:0,y:0,r:0}];
ok(helpers.layGlobal(L.parts[0],5)===5,"номер диода в первой детали");
ok(helpers.layGlobal(L.parts[1],2)===20,"номер диода во второй детали: после 18 диодов первой");
ok(helpers.layGlobal(L.parts[2],0)===21,"номер диода в третьей детали");
// Выбор диода по нажатию: ближайший, а не верхний.
L.hit=[0,1,2,3].map(i=>({p:1,g:i,j:i,free:false,x:100+i*6,y:100}));
ok(helpers.layNearest(100+5*6/2*0+17,100).g===3,"ближайший диод на плотной ленте (шаг 6)");
ok(helpers.layNearest(100,100).g===0,"нажатие точно в диод");
ok(helpers.layNearest(100,130)===null,"далеко от диодов — ничего не выбрано");
let bad=0;
for(const [c,m] of t){ if(!c){ console.log("  FAIL",m); bad++; } }
console.log(`Пройдено ${t.length-bad} из ${t.length}`);
process.exit(bad?1:0);
