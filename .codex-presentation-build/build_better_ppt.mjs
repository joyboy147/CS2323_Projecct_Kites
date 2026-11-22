import fs from "node:fs/promises";
import path from "node:path";
import { Presentation, PresentationFile } from "@oai/artifact-tool";

const W = 1280, H = 720;
const OUT = "/home/chaya_147/Downloads/PPT Making Slides/KITES_Final_Presentation.pptx";
const BUILD = "/home/chaya_147/Downloads/CS2323_Projecct_Kites-main/.codex-presentation-build/better-ppt";
const ASSET = "/home/chaya_147/Downloads/PPT Making Slides";

const C = {
  navy: "#0B1F33", ink: "#18324A", blue: "#1976D2", cyan: "#00A7B5",
  green: "#179C6B", red: "#E5534B", amber: "#E39A20", bg: "#F6F8FB",
  pale: "#EAF2FA", pale2: "#E8F7F4", line: "#D7E1EB", white: "#FFFFFF",
  muted: "#5E7185", dark: "#101820"
};
const FONT = "Aptos";

async function imageBytes(file) {
  const b = await fs.readFile(file);
  return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
}

function box(slide, x, y, w, h, fill=C.white, line=C.line, radius="rounded") {
  return slide.shapes.add({geometry: radius === "rounded" ? "roundRect" : "rect", position:{left:x,top:y,width:w,height:h}, fill, line:{style:"solid",fill:line,width:1}});
}
function text(slide, value, x, y, w, h, size=20, color=C.ink, bold=false, align="left", font=FONT) {
  const s = slide.shapes.add({geometry:"textbox", position:{left:x,top:y,width:w,height:h}, fill:"none", line:{style:"solid",fill:"none",width:0}});
  s.text = value;
  s.text.style = {fontFamily:font, fontSize:`${size}pt`, color, bold, alignment:align, verticalAlignment:"middle", wrap:true};
  return s;
}
function title(slide, n, value, sub="") {
  text(slide, String(n).padStart(2,"0"), 54, 30, 50, 24, 11, C.blue, true);
  text(slide, value, 54, 54, 1120, 54, 30, C.navy, true);
  if (sub) text(slide, sub, 56, 108, 1120, 28, 13, C.muted, false);
  slide.shapes.add({geometry:"rect", position:{left:54,top:142,width:1172,height:2}, fill:C.line, line:{fill:"none",width:0}});
}
function footer(slide, n, source="") {
  if (source) text(slide, source, 54, 686, 1030, 18, 9, C.muted, false);
  text(slide, `${n}/13`, 1160, 686, 66, 18, 9, C.muted, false, "right");
}
function metric(slide, x, y, w, label, value, accent=C.blue, note="", h=132) {
  box(slide,x,y,w,h,C.white,C.line);
  slide.shapes.add({geometry:"rect",position:{left:x,top:y,width:6,height:h},fill:accent,line:{fill:"none",width:0}});
  if (h < 110) {
    text(slide,label.toUpperCase(),x+18,y+7,w-34,18,8,C.muted,true);
    text(slide,value,x+18,y+24,w-34,38,20,accent,true);
    if(note) text(slide,note,x+18,y+62,w-34,h-66,8,C.ink,false);
  } else {
    text(slide,label.toUpperCase(),x+20,y+14,w-38,22,10,C.muted,true);
    text(slide,value,x+20,y+39,w-38,54,27,accent,true);
    if(note) text(slide,note,x+20,y+94,w-38,26,11,C.ink,false);
  }
}
function bullet(slide, value, x, y, w, accent=C.blue, size=17) {
  slide.shapes.add({geometry:"ellipse",position:{left:x,top:y+9,width:9,height:9},fill:accent,line:{fill:"none",width:0}});
  text(slide,value,x+20,y,w-20,44,size,C.ink,false);
}
async function addImage(slide, file, x,y,w,h, fit="contain", alt="") {
  slide.images.add({blob:await imageBytes(file),contentType:"image/png",alt,fit,position:{left:x,top:y,width:w,height:h}});
}
function notes(slide, lines) { slide.speakerNotes.text = lines; slide.speakerNotes.setVisible(true); }

const p = Presentation.create({slideSize:{width:W,height:H}});

// 1 Cover
{
  const s=p.slides.add(); s.background.fill=C.bg;
  s.shapes.add({geometry:"rect",position:{left:0,top:0,width:18,height:H},fill:C.blue,line:{fill:"none",width:0}});
  text(s,"KITES",62,54,300,40,14,C.blue,true);
  text(s,"Instruction Reordering\nand Stall Analysis",62,152,700,160,46,C.navy,true);
  text(s,"A measured, explainable extension to the five-stage RISC-V simulator",64,330,660,58,19,C.muted,false);
  // pipeline motif
  const labels=["IF","ID","EX","MEM","WB"];
  labels.forEach((v,i)=>{box(s,790+i*82,205,66,48,i===2?C.blue:C.white,i===2?C.blue:C.line);text(s,v,790+i*82,205,66,48,13,i===2?C.white:C.ink,true,"center");});
  text(s,"Nandan Nihal Chanda  ·  CS25BTECH11014",64,610,700,30,14,C.ink,true);
  text(s,"CS2323 Computer Architecture",64,644,500,24,11,C.muted,false);
  footer(s,1);
  notes(s,["Open with the outcome: KITES can now diagnose stalls, suggest a safe instruction order, and verify the improvement on the simulator.","Timing: 20 seconds."]);
}

// 2 Why reorder
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,2,"Why instruction reordering?","Useful independent work can occupy cycles that a dependency would otherwise waste");
  box(s,56,180,535,350,C.white,C.line);
  text(s,"Original",82,198,150,26,12,C.red,true);
  text(s,"lw   x3, 0(x1)\nadd  x6, x3, x1\naddi x7, x0, 1",82,238,430,140,22,C.dark,false,"left","IBM Plex Mono");
  text(s,"The add must wait for x3",82,415,410,34,18,C.red,true);
  text(s,"A pipeline bubble performs no useful work",82,458,430,50,14,C.muted,false);
  box(s,635,180,535,350,C.white,C.line);
  text(s,"Safe order",661,198,150,26,12,C.green,true);
  text(s,"lw   x3, 0(x1)\naddi x7, x0, 1\nadd  x6, x3, x1",661,238,430,140,22,C.dark,false,"left","IBM Plex Mono");
  text(s,"Independent work fills the gap",661,415,430,34,18,C.green,true);
  text(s,"Program state remains unchanged",661,458,430,50,14,C.muted,false);
  text(s,"Reordering targets data-stall cycles. Control-flush cycles need different optimisations.",56,566,1114,46,19,C.navy,true,"center");
  footer(s,2,"Source: project scope document and KITES hazard model");
  notes(s,["Explain only the motivation. A load produces its value late, so the dependent add waits. An independent addi can run during that wait.","Stress that semantic equivalence is mandatory.","Timing: 45 seconds."]);
}

// 3 Algorithm
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,3,"Static heuristic scheduling","The implementation follows Lecture 14, with KITES itself supplying the stall cost");
  await addImage(s,path.join(ASSET,"Screenshot From 2026-11-20 19-54-48.png"),58,180,555,405,"contain","Lecture 14 slide: minimising stalls");
  const steps=["Split the program into basic blocks","Build a dependency DAG","Choose a ready instruction that avoids a stall","Prefer the longest remaining dependency chain"];
  steps.forEach((v,i)=>{text(s,String(i+1),666,185+i*88,42,42,16,C.white,true,"center");s.shapes.add({geometry:"ellipse",position:{left:666,top:185+i*88,width:42,height:42},fill:i===2?C.green:C.blue,line:{fill:"none",width:0}});text(s,String(i+1),666,185+i*88,42,42,16,C.white,true,"center");text(s,v,726,180+i*88,430,54,17,C.ink,i===2);});
  box(s,660,548,505,62,C.pale2,C.green);
  text(s,"Key adaptation: call KITES' exact hazard check instead of estimating latency",680,558,465,42,14,C.green,true,"center");
  footer(s,3,"Source: Cambridge Optimising Compilers, Lecture 14; Phase 2 report");
  notes(s,["Use the supplied Lecture 14 slide to explain the ready-list heuristic.","Our adaptation replaces a generic latency table with KITES' own hazard decision, so the model matches the simulator.","Timing: 55 seconds."]);
}

// 4 Correctness DAG
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,4,"Correctness through the dependency DAG","An instruction moves only when every required ordering edge remains satisfied");
  await addImage(s,path.join(ASSET,"Screenshot From 2026-11-20 19-54-37.png"),58,173,605,440,"contain","Lecture 14 slide: preserving dependencies");
  const items=[["RAW","true data dependency",C.red],["WAR / WAW","register name dependencies",C.blue],["Memory order","loads and stores remain ordered",C.green],["Control barriers","branches and block boundaries",C.amber]];
  items.forEach((it,i)=>{box(s,715,185+i*96,430,72,C.white,C.line);s.shapes.add({geometry:"rect",position:{left:715,top:185+i*96,width:6,height:72},fill:it[2],line:{fill:"none",width:0}});text(s,it[0],738,194+i*96,155,24,14,it[2],true);text(s,it[1],738,220+i*96,370,28,13,C.ink,false);});
  text(s,"Result: a different schedule, not a different program",715,585,430,34,18,C.navy,true,"center");
  footer(s,4,"Source: Cambridge Optimising Compilers, Lecture 14; project scope document");
  notes(s,["Briefly name RAW, WAR, WAW, memory, and control constraints.","The DAG makes legality a precondition, not a post-hoc guess.","Timing: 45 seconds."]);
}

// 5 UI demo
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,5,"Suggestions UI on Bubble Sort + Sum","The screen connects a moved source line to measured cycle savings and an equivalence check");
  await addImage(s,path.join(ASSET,"Suggestions.png"),50,158,1180,408,"contain","KITES Suggestions tab showing original and suggested code");
  metric(s,74,575,300,"Measured cycles","838 → 756",C.green,"82 cycles saved without forwarding",95);
  metric(s,398,575,300,"Data stalls","160 → 76",C.blue,"84 fewer stall cycles",95);
  metric(s,722,575,440,"Safety check","Identical ✓",C.green,"Final registers and data memory match",95);
  footer(s,5,"Source: supplied KITES Suggestions screenshot and More_info.txt");
  notes(s,["Point to the side-by-side code and green moved rows.","The banner reports measured simulator cycles, not only model predictions.","Explain Apply to editor and per-block checkboxes in one sentence.","Timing: 65 seconds."]);
}

// 6 results chart
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,6,"Measured improvement depends on forwarding","Reordering helps most when the processor cannot forward a result to the next instruction");
  s.charts.add("bar",{position:{left:70,top:190,width:720,height:360},categories:["With forwarding","Without forwarding"],series:[{name:"Original cycles",values:[695,838],fill:C.muted},{name:"Reordered cycles",values:[680,756],fill:C.green}],hasLegend:true,legend:{position:"bottom"},dataLabels:{showValue:true,position:"outEnd"},xAxis:{majorGridlines:{style:"solid",fill:C.line,width:1}},yAxis:{title:{text:"Configuration"}}});
  metric(s,850,205,330,"With forwarding","2.2% faster",C.blue,"695 → 680 cycles");
  metric(s,850,365,330,"Without forwarding","9.8% faster",C.green,"838 → 756 cycles");
  text(s,"Stalls: 15 → 0",860,535,310,28,15,C.blue,true,"center");
  text(s,"Stalls: 160 → 76",860,568,310,28,15,C.green,true,"center");
  footer(s,6,"Source: More_info.txt; measured on Bubble Sort + Sum");
  notes(s,["State the result first: 9.8 percent fewer cycles without forwarding and 2.2 percent with forwarding.","Forwarding already hides many RAW hazards, leaving less room for scheduling.","Timing: 45 seconds."]);
}

// 7 profiler analysis
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,7,"Stall Profiler analysis","The profiler attributes every lost cycle to a source line and a named cause");
  await addImage(s,path.join(ASSET,"Profiler.png"),50,158,1180,408,"contain","KITES Profiler tab with lost cycles by line and cause");
  metric(s,72,575,342,"Bubble Sort + Sum","341 / 838 lost",C.red,"40.7% of cycles",95);
  metric(s,432,575,342,"Largest causes","158 RAW + 135 flush",C.amber,"No forwarding configuration",95);
  metric(s,792,575,388,"Accounting","Zero unclassified",C.green,"cycles = retired + stalls + flushes + fill",95);
  footer(s,7,"Source: supplied KITES Profiler screenshot; Phase 3 report");
  notes(s,["Explain the heat gutter, ranked lines, producer column, and Suggest fix button.","For GCD, Taylor Series, and Sort lab programs, use the same workflow: identify the top line, inspect the cause, then check whether a legal independent instruction exists.","Timing: 55 seconds."]);
}

// 8 novelty
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,8,"Uniqueness and novelty","The project closes the loop from diagnosis to a verified source-code change");
  const items=[
    ["KITES-exact cost model","The scheduler reuses the simulator's own hazard decision, including forwarding modes and quirks.",C.blue],
    ["Explainable source rewrite","Each move names the blocked instruction, the filler, the saved stalls, and why the move is safe.",C.green],
    ["Real-run visual evidence","Pipeline diagrams and profiler totals come from KITES traces, not a separate approximation.",C.cyan],
    ["Interactive verification","Users accept blocks individually; KITES reassembles, reruns, and checks state before Apply.",C.amber]
  ];
  items.forEach((it,i)=>{const x=i%2?650:70,y=i<2?185:390;box(s,x,y,560,170,C.white,C.line);s.shapes.add({geometry:"rect",position:{left:x,top:y,width:7,height:170},fill:it[2],line:{fill:"none",width:0}});text(s,it[0],x+28,y+22,500,34,18,it[2],true);text(s,it[1],x+28,y+66,500,78,14,C.ink,false);});
  text(s,"Novel contribution: a teaching simulator that explains, changes, measures, and verifies the same program",75,602,1125,48,19,C.navy,true,"center");
  footer(s,8,"Source: Phase 2 and Phase 3 reports");
  notes(s,["This begins the required final three-to-five-minute section.","Emphasise the closed diagnostic loop and exact agreement with KITES.","Timing: 45 seconds."]);
}

// 9 verification
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,9,"Verification approach and test coverage","Correctness was checked at dependency, simulator, architectural-state, and fuzzing levels");
  metric(s,65,180,260,"Automated suite","203 / 203",C.green,"headless tests pass");
  metric(s,345,180,260,"Accounting runs","328",C.blue,"zero unclassified cycles");
  metric(s,625,180,260,"Scheduler fuzzing","≈7,500",C.cyan,"random programs");
  metric(s,905,180,310,"Selection fuzzing","7,316 subsets",C.amber,"integer + FP modes");
  bullet(s,"Dependency legality: every scheduled order respects all DAG edges",80,360,530,C.blue,16);
  bullet(s,"Model agreement: predicted cycles equal measured KITES cycles",80,425,530,C.green,16);
  bullet(s,"Equivalence: final registers and memory match after rewriting",650,360,530,C.cyan,16);
  bullet(s,"Independent reference: RVSS single-cycle execution checks program meaning",650,425,530,C.amber,16);
  box(s,80,515,1100,86,C.pale2,C.green);
  text(s,"Phase 3 differential review: 900 before/after comparisons, with no behavioural differences after refactoring",110,530,1040,54,17,C.green,true,"center");
  footer(s,9,"Source: Phase 3 report, Sections 6.2–6.3");
  notes(s,["Give the numbers cleanly. 203 tests, 328 accounting runs, about 7,500 scheduler fuzz programs, and 7,316 selection subsets.","Mention that the single-cycle RVSS processor serves as an architectural reference.","Timing: 50 seconds."]);
}

// 10 LOC contribution
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,10,"Engineering footprint and contribution","The submitted work is a single-member project built inside an existing KITES codebase");
  metric(s,72,185,330,"Phase 3 diff","+6,410 / −305",C.blue,"172 files changed");
  metric(s,425,185,330,"Core extension","3,854 added lines",C.green,"src, tests, docs, examples");
  metric(s,778,185,430,"Headless harness","2,554 added lines",C.cyan,"testing tools and Qt stand-ins");
  box(s,72,365,1136,205,C.white,C.line);
  text(s,"Nandan Nihal Chanda",102,392,430,42,25,C.navy,true);
  text(s,"CS25BTECH11014  ·  sole project member",102,438,430,30,14,C.muted,false);
  bullet(s,"Designed the scheduler, dependency model, exact KITES cost model, and source rewriter",570,385,600,C.blue,15);
  bullet(s,"Integrated the Suggestions UI, pipeline diagrams, and Stall Profiler",570,445,600,C.green,15);
  bullet(s,"Built verification tools, fuzzing, tests, documentation, and sample programs",570,505,600,C.cyan,15);
  text(s,"Existing KITES code and bundled Lua are excluded from the contribution figures.",72,602,1136,30,12,C.muted,false,"center");
  footer(s,10,"Source: Phase 3 report, Introduction and Table 1");
  notes(s,["Clarify that the project has one student member, so the team-contribution item maps to Nandan's contribution.","Distinguish Phase 3 diff figures from the pre-existing KITES repository.","Timing: 45 seconds."]);
}

// 11 learnings challenges
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,11,"Key learnings and major challenges","The hardest part was matching an existing simulator exactly, including its edge cases");
  text(s,"KEY LEARNINGS",76,180,500,26,12,C.blue,true);
  bullet(s,"List scheduling: basic blocks, ready lists, critical paths, and safe dependence edges",78,220,520,C.blue,16);
  bullet(s,"Simulator reuse beats an approximate latency table when exact cycle agreement matters",78,300,520,C.green,16);
  bullet(s,"Unit tests were necessary, but independent review and differential fuzzing found additional bugs",78,380,520,C.cyan,16);
  bullet(s,"Source-line scheduling kept explanations and rewriting understandable",78,460,520,C.amber,16);
  text(s,"MAJOR CHALLENGES",666,180,500,26,12,C.red,true);
  bullet(s,"KITES hazard-unit quirks: raw-field aliases, FP/GPR interactions, and auipc behaviour",668,220,520,C.red,16);
  bullet(s,"Cross-block cost interactions and partial per-block selections",668,300,520,C.amber,16);
  bullet(s,"Attributing stalls versus wrong-path work and control flushes without double counting",668,380,520,C.blue,16);
  bullet(s,"Keeping long profiles responsive with cancellation and bounded undo history",668,460,520,C.green,16);
  footer(s,11,"Source: Phase 2 report Section 9; Phase 3 report Sections 2, 6 and 8");
  notes(s,["Pair each learning with the challenge that produced it.","Mention that the Phase 1 latency approximation had to be replaced after review.","Timing: 50 seconds."]);
}

// 12 limitations/extensions
{
  const s=p.slides.add(); s.background.fill=C.bg; title(s,12,"Limitations and possible extensions","Current safeguards favour correctness and explainability over aggressive optimisation");
  text(s,"CURRENT LIMITS",76,178,500,28,12,C.red,true);
  const limits=["Scheduling stays inside basic blocks","FP instructions are barriers by default","Memory operations remain ordered","The static model counts each block once","Computed jumps into a block are invisible"];
  limits.forEach((v,i)=>bullet(s,v,78,220+i*63,500,C.red,16));
  text(s,"POSSIBLE EXTENSIONS",666,178,500,28,12,C.green,true);
  const exts=["Loop-aware weighting and safe unrolling","Branch analysis and branchless rewrites","FP scheduling after isolating KITES quirks","Broader or dual-issue pipeline models","Cache-aware scheduling objectives"];
  exts.forEach((v,i)=>bullet(s,v,668,220+i*63,500,C.green,16));
  box(s,145,570,990,64,C.pale,C.blue);
  text(s,"Highest-value next step: loop-aware optimisation, because repeated branch flushes dominate Bubble Sort",170,582,940,40,17,C.blue,true,"center");
  footer(s,12,"Source: scope document; Phase 3 report Section 9");
  notes(s,["State that loop unrolling was designed but descoped because it needs trip-count, induction-variable, and liveness analysis.","The current scheduler deliberately keeps memory order conservative.","Timing: 45 seconds."]);
}

// 13 Thank you
{
  const s=p.slides.add(); s.background.fill=C.navy;
  text(s,"KITES",62,54,300,40,14,"#63B3FF",true);
  text(s,"Thank you",62,208,800,90,50,C.white,true);
  text(s,"Questions?",64,310,500,48,24,"#A7C7E7",false);
  box(s,780,180,400,260,"#132F49","#31516E");
  text(s,"Main result",812,204,336,28,12,"#A7C7E7",true);
  text(s,"838 → 756",812,246,336,70,34,"#62D6A5",true,"center");
  text(s,"cycles without forwarding",812,315,336,30,14,C.white,false,"center");
  text(s,"203/203 tests  ·  zero unclassified cycles",812,372,336,34,13,"#A7C7E7",true,"center");
  text(s,"Nandan Nihal Chanda  ·  CS25BTECH11014",64,630,700,30,14,C.white,true);
  footer(s,13);
  notes(s,["Close on the two strongest facts: measured cycle reduction and comprehensive verification.","Invite questions.","Timing: 15 seconds."]);
}

await fs.mkdir(BUILD,{recursive:true});
await fs.mkdir(path.dirname(OUT),{recursive:true});
for (let i=0;i<p.slides.items.length;i++) {
  const png=await p.export({slide:p.slides.items[i],format:"png",scale:1});
  await fs.writeFile(path.join(BUILD,`slide-${String(i+1).padStart(2,"0")}.png`),new Uint8Array(await png.arrayBuffer()));
}
const pptx=await PresentationFile.exportPptx(p);
await pptx.save(OUT);
console.log(JSON.stringify({out:OUT,build:BUILD,slides:p.slides.items.length}));
