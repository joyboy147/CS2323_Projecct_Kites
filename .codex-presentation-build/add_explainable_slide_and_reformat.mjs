import fs from "node:fs/promises";
import { FileBlob, PresentationFile } from "@oai/artifact-tool";

const source="/home/chaya_147/Downloads/PPT Making Slides/KITES_Presentation_With_Script.pptx";
const output="/home/chaya_147/Downloads/PPT Making Slides/KITES_Final_14_Slides_With_Script.pptx";
const screenshot="/home/chaya_147/Downloads/PPT Making Slides/Suggestions.png";
const p=await PresentationFile.importPptx(await FileBlob.load(source));
const C={navy:"#0B1F33",ink:"#18324A",blue:"#1976D2",green:"#179C6B",cyan:"#00A7B5",red:"#E5534B",amber:"#E39A20",bg:"#F6F8FB",pale:"#EAF2FA",pale2:"#E8F7F4",line:"#D7E1EB",white:"#FFFFFF",muted:"#5E7185"};
const FONT="Arial";

function shape(slide,geometry,left,top,width,height,fill,line="none"){
  return slide.shapes.add({geometry,position:{left,top,width,height},fill,line:{style:"solid",fill:line,width:line==="none"?0:1}});
}
function text(slide,value,left,top,width,height,size=18,color=C.ink,bold=false,align="left"){
  const o=slide.shapes.add({geometry:"textbox",position:{left,top,width,height},fill:"none",line:{fill:"none",width:0}});
  o.text=value;o.text.style={fontFamily:FONT,fontSize:`${size}pt`,color,bold,alignment:align,verticalAlignment:"middle",wrap:true};return o;
}
function bullet(slide,value,x,y,w,color){shape(slide,"ellipse",x,y+9,9,9,color);text(slide,value,x+22,y,w-22,54,16,C.ink,false);}

async function allTextboxes(){
  const r=await p.inspect({kind:"textbox",maxChars:200000});
  return r.ndjson.split("\n").filter(Boolean).map(x=>JSON.parse(x));
}
async function setExact(oldText,newText){
  const boxes=await allTextboxes();
  const hit=boxes.find(x=>x.text===oldText);
  if(!hit) throw new Error(`Text not found: ${oldText}`);
  p.resolve(hit.id).text=newText;
}

// Simplify automated-test claims so they are easier to present accurately.
await setExact("≈7,500","Thousands");
await setExact("random programs","automatically generated programs");
await setExact("7,316 subsets","Many combinations");
await setExact("integer + FP modes","different accepted-block selections");
await setExact("Independent regression check: 900 before/after comparisons reported no behavioural differences after refactoring","Regression check: the same programs were run before and after code cleanup, and their outputs matched");
await setExact("Highest-value next step: loop-aware optimisation, because repeated branch flushes dominate Bubble Sort","Next priority before submission: loop-aware optimisation and safe loop unrolling");

// Insert a new explanation slide after the results slide and before the Stall Profiler.
const {slide:explainSlide}=p.slides.insert({after:p.slides.items[5]});
explainSlide.background.fill=C.bg;
text(explainSlide,"07",54,30,50,24,11,C.blue,true);
text(explainSlide,"Explainable source rewrite",54,54,1120,54,30,C.navy,true);
text(explainSlide,"The suggestion tells the user what moved, why it saves cycles, and why the move is safe",56,108,1120,28,13,C.muted,false);
shape(explainSlide,"rect",54,142,1172,2,C.line);
const bytes=await fs.readFile(screenshot);
explainSlide.images.add({blob:bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength),contentType:"image/png",alt:"KITES Why these moves explanation for reordered instructions",fit:"cover",crop:{left:0.02,top:0.56,right:0.19,bottom:0.01},position:{left:70,top:170,width:1140,height:330},geometry:"roundRect",borderRadius:"rounded-lg"});
text(explainSlide,"The explanation connects the performance gain to one concrete source-code move",80,520,1120,36,18,C.navy,true,"center");
bullet(explainSlide,"What was waiting: the dependent instruction and its hazard",115,570,330,C.red);
bullet(explainSlide,"What moved: an independent instruction that fills the waiting slot",465,570,350,C.blue);
bullet(explainSlide,"Why it is safe: no register or memory dependency is crossed",835,570,350,C.green);
text(explainSlide,"Source: supplied KITES Suggestions screenshot",54,686,1030,18,9,C.muted,false);
text(explainSlide,"7/14",1160,686,66,18,9,C.muted,false,"right");

// Rebuild contribution slide as a clean two-column attribution, without the student's name.
const contribution=p.slides.items[10];
contribution.shapes.deleteAll();
contribution.background.fill=C.bg;
text(contribution,"11",54,30,50,24,11,C.blue,true);
text(contribution,"Contribution",54,54,1120,54,30,C.navy,true);
text(contribution,"Clear division between my work and LLM assistance",56,108,1120,28,13,C.muted,false);
shape(contribution,"rect",54,142,1172,2,C.line);

shape(contribution,"roundRect",65,185,555,410,C.white,C.line);
shape(contribution,"rect",65,185,7,410,C.blue);
text(contribution,"MY WORK",96,208,485,34,18,C.blue,true);
bullet(contribution,"Studied static scheduling, basic blocks, dependency graphs, and the lecture heuristic",96,266,485,C.blue);
bullet(contribution,"Attempted the core static scheduler in C++ with Codex guidance",96,344,485,C.blue);
bullet(contribution,"Selected the lab examples, ran manual tests, checked results, and reviewed the suggestions",96,422,485,C.blue);
bullet(contribution,"Decided the project scope, verified the final behaviour, and accepted the submitted changes",96,500,485,C.blue);

shape(contribution,"roundRect",660,185,555,410,C.white,C.line);
shape(contribution,"rect",660,185,7,410,C.green);
text(contribution,"LLM ASSISTANCE",691,208,485,34,18,C.green,true);
bullet(contribution,"Claude and Codex assisted implementation, debugging, and code review",691,266,485,C.green);
bullet(contribution,"The Suggestions UI, Stall Profiler UI, and integration work were mainly AI-assisted",691,344,485,C.green);
bullet(contribution,"LLMs helped create automated tests, verification tools, documentation, and report drafts",691,422,485,C.green);
bullet(contribution,"I checked AI-generated work against KITES runs, tests, and manual observations",691,500,485,C.green);
text(contribution,"Implementation size is shown separately from contribution ownership",72,618,1136,28,12,C.muted,false,"center");
text(contribution,"Source: current repository snapshot and project reports",54,686,1030,18,9,C.muted,false);
text(contribution,"11/14",1160,686,66,18,9,C.muted,false,"right");

// Renumber slide labels and page markers after inserting the new slide.
const boxes=await allTextboxes();
for(const b of boxes){
  const pos=b.position||{};
  if(Math.abs((pos.left??-999)-54)<1 && Math.abs((pos.top??-999)-30)<1 && Math.abs((pos.width??0)-50)<2){
    p.resolve(b.id).text=String(b.slideIndex+1).padStart(2,"0");
  }
  if(Math.abs((pos.left??-999)-1160)<1 && Math.abs((pos.top??-999)-686)<1){
    p.resolve(b.id).text=`${b.slideIndex+1}/14`;
  }
}

const scripts=[
`Good morning. My project extends the KITES RISC-V simulator with instruction reordering and stall analysis. The goal is to show where pipeline cycles are wasted, suggest a safe instruction order, and measure the result. I will first explain the scheduler and interface, then cover the results, verification, contributions, learnings, and limitations. [25 seconds]`,
`Why do we need reordering? Here, the add immediately depends on the loaded value in x3, so it has to wait. The addi instruction is independent, so it can be placed between the load and the dependent add. It performs useful work during a cycle that would otherwise become a stall. Dependency checking confirms that this move does not change the program result. [35 seconds]`,
`Correctness uses a dependency graph. A basic block is a consecutive group of instructions with no branch or jump in the middle. Instructions become nodes, and edges represent read-after-write, write-after-read, write-after-write, memory, and control restrictions. The scheduler may change the order only when every edge remains satisfied. [40 seconds]`,
`The scheduler follows the static heuristic from Lecture 14. It divides the source into basic blocks, builds the dependency graph, and keeps a ready list of instructions whose dependencies are satisfied. It chooses a ready instruction that avoids a stall and uses the remaining critical path to break ties. The stall estimate follows KITES' own hazard behaviour. [45 seconds]`,
`This is Bubble Sort plus Sum from the lab exam. The original code is on the left and the suggested order is on the right. Green rows show moved instructions. The important feature is that the tool does not only say what to move. It explains which instruction was waiting, which independent instruction can fill that waiting slot, and why crossing the other lines is safe. The user can select suggestions block by block and apply them only after KITES checks the final registers and memory. Here, cycles reduce from 838 to 756 and stalls reduce from 160 to 76. [65 seconds]`,
`This graph compares the same example with and without forwarding. With forwarding, cycles reduce from 695 to 680. Without forwarding, they reduce from 838 to 756. The improvement is larger without forwarding because the processor has more waiting slots that independent instructions can fill. [35 seconds]`,
`This slide zooms into the explanation under the suggested code. It tells the user three things. First, which instruction was waiting and why. Second, which independent instruction moved into that slot. Third, why the move is safe, meaning no required register or memory dependency was crossed. This is what I mean by an explainable source rewrite: the tool changes source code, but it also gives the reason for the change in terms the user can verify. [45 seconds]`,
`The Stall Profiler explains where the execution time goes. In this no-forwarding run, there are 838 total cycles: 497 useful instruction retirements, 158 data stalls, 179 branch or jump flush cycles, and 4 pipeline fill and drain cycles. The 341 overhead cycles are the last three categories. Reordering mainly targets the data stalls, while branch flushes usually need loop or branch optimisation. [45 seconds]`,
`The novelty is the full connection between diagnosis and a verified source-code change. The tool explains the move, predicts the effect using KITES' hazard behaviour, shows measurements from simulator runs, and checks the final state before applying the rewrite. This helps students understand both why performance changes and why the move is legal. [35 seconds]`,
`Verification combines directed tests, automatically generated programs, simulator comparisons, and manual checks. The dependency tests check legal schedules. KITES measurements check the predicted cycle counts. Register and memory comparisons check program equivalence. When I say thousands of automatically generated programs, I mean a script created many small instruction sequences and checked them automatically. I did not write or inspect thousands of programs manually. The regression check means the same test programs were run before and after code cleanup, and matching outputs showed that the cleanup did not change behaviour. [55 seconds]`,
`My contribution and the LLM contribution are separated here. I studied the scheduling method, attempted the core C++ scheduler with guidance, selected the examples, ran manual tests, reviewed the suggestions, and made the final decisions. Claude and Codex assisted substantially with implementation, debugging, the interfaces, automated verification, documentation, and report drafting. I checked the AI-generated work against KITES runs and tests. [45 seconds]`,
`My technical learning includes list scheduling, basic blocks, dependency graphs, ready lists, and critical paths. I learned to connect those ideas to actual KITES stalls and cycle counts. I also improved my Claude Code and Codex skills, including the use of subagents and reviewing AI-generated code. The main challenges were KITES-specific hazard quirks, interactions across block boundaries, separating stalls from branch flushes correctly, and keeping long profiling runs responsive. [50 seconds]`,
`The current scheduler is conservative. It only reorders inside basic blocks. Floating-point instructions are usually barriers, memory operations remain ordered, computed jumps are difficult to analyse, and the static model does not know how often every block runs. Possible extensions include loop-aware weighting, safe loop unrolling, branch analysis, floating-point scheduling after handling KITES quirks, wider pipelines, and cache-aware scheduling. My next priority before submission is loop-aware optimisation and safe loop unrolling. [50 seconds]`,
`That concludes my presentation. Thank you, and I am ready for questions. [10 seconds]`
];
if(scripts.length!==p.slides.items.length) throw new Error(`Expected 14 scripts, got ${scripts.length}`);
for(let i=0;i<scripts.length;i++){p.slides.items[i].speakerNotes.text=scripts[i];p.slides.items[i].speakerNotes.setVisible(true);}

await (await PresentationFile.exportPptx(p)).save(output);
console.log(output);
