import fs from "node:fs/promises";
import { FileBlob, PresentationFile } from "@oai/artifact-tool";

const source = "/home/chaya_147/Downloads/PPT Making Slides/KITES_Final_Presentation.pptx";
const draft = "/home/chaya_147/Downloads/CS2323_Projecct_Kites-main/.codex-presentation-build/KITES_clarity_revision_draft.pptx";
const output = "/home/chaya_147/Downloads/PPT Making Slides/KITES_Final_Presentation_Clarified.pptx";
const p = await PresentationFile.importPptx(await FileBlob.load(source));

const replacements = new Map([
  ["sh/9s7a5gfm", "The profiler separates useful instruction cycles from pipeline overhead"],
  ["sh/q9sr21sf", "341 overhead cycles"],
  ["sh/fydwnm50", "out of 838 total cycles in the HNF run"],
  ["sh/t4jmlgn6", "158 stalls + 179 flush"],
  ["sh/utk3il0b", "plus 4 pipeline fill cycles"],
  ["sh/6lgbyx8b", "COMPLETE CYCLE BREAKDOWN"],
  ["sh/9gru5oby", "All 838 explained"],
  ["sh/2pgf6hk7", "497 retired + 158 stalls + 179 flush + 4 fill"],
  ["sh/one9ob6p", "Source: supplied KITES Profiler screenshot and current project report"],
  ["sh/wr2dcfy5", "Correctness checks combine directed tests, random programs, and simulator comparisons"],
  ["sh/xkv6t8ze", "RANDOM-PROGRAM TESTING"],
  ["sh/5gju107a", "SELECTION-COMBINATION TESTING"],
  ["sh/nml4jqdo", "Independent regression check: 900 before/after comparisons reported no behavioural differences after refactoring"],
  ["sh/ehgz2xwb", "Current repository snapshot and an honest division of student and LLM contributions"],
  ["sh/y5wvyp4f", "IMPLEMENTATION CODE"],
  ["sh/iloz2h0j", "6,683 lines"],
  ["sh/ji5ofux4", "scheduler and Suggestions/Profiler UI"],
  ["sh/ojq9kbqd", "TEST CODE"],
  ["sh/1k7exsbe", "1,691 lines"],
  ["sh/1kfm1kjy", "CURRENT TEST SUITE"],
  ["sh/2tczelcf", "203 / 203 pass"],
  ["sh/sbyhwvah", "CS25BTECH11014  ·  student owner and reviewer"],
  ["sh/r2ho7i1c", "Nandan: defined the problem and approach, studied the scheduling method, supplied lab programs, reviewed results, and made final project decisions"],
  ["sh/grmd8ru5", "Claude and Codex: assisted with implementation, debugging, UI integration, verification tools, tests, documentation, and report drafting"],
  ["sh/jel8byds", "UI work was mainly AI-assisted; Nandan validated the behaviour and accepted the final changes"],
  ["sh/uhknitcr", "LLM assistance: Claude and Codex. Claims and measurements were checked against KITES runs and tests."],
]);

for (const [id, value] of replacements) {
  const obj = p.resolve(id);
  if (!obj) throw new Error(`Missing object ${id}`);
  obj.text = value;
}

// Reduce contribution bullet text slightly to fit the existing layout.
for (const id of ["sh/r2ho7i1c", "sh/grmd8ru5", "sh/jel8byds"]) {
  const obj = p.resolve(id);
  obj.text.style = {...obj.text.style, fontSize:"13pt"};
}

p.slides.items[6].speakerNotes.text = [
  "HNF means the five-stage processor with hazard detection and no forwarding.",
  "The 838 total cycles split into 497 useful retirements, 158 data-stall cycles, 179 control-flush cycles, and 4 pipeline fill/drain cycles.",
  "The 341 overhead cycles are 158 + 179 + 4. They are not all removable by instruction reordering.",
  "Timing: 55 seconds."
];
p.slides.items[8].speakerNotes.text = [
  "Fuzzing means automatically generating many varied or random inputs and checking safety properties.",
  "Scheduler random-program testing checks generated programs. Selection-combination testing checks many combinations of accepted blocks.",
  "Timing: 50 seconds."
];
p.slides.items[9].speakerNotes.text = [
  "Be explicit about contribution. Nandan owns the scope, understanding, review, experiments, and final decisions.",
  "Claude and Codex substantially assisted implementation, especially the UI, testing tools, debugging, documentation, and report drafting.",
  "The line counts describe the current repository snapshot, not a completed Phase 3 submission.",
  "Timing: 45 seconds."
];

await fs.mkdir(new URL(".", `file://${draft}`).pathname, {recursive:true}).catch(()=>{});
await (await PresentationFile.exportPptx(p)).save(draft);
await (await PresentationFile.exportPptx(p)).save(output);
console.log(JSON.stringify({draft, output}));
