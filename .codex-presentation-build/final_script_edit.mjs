import { FileBlob, PresentationFile } from "@oai/artifact-tool";

const source = "/home/chaya_147/Downloads/PPT Making Slides/KITES_Final_Presentation_Clarified_v2.pptx";
const output = "/home/chaya_147/Downloads/PPT Making Slides/KITES_Presentation_With_Script.pptx";
const p = await PresentationFile.importPptx(await FileBlob.load(source));

const edits = {
  "sh/10by5gnq": "Each suggestion shows what moved, which stall it fills, and why the result remains correct",
  "sh/g3qpkzux": "Learned list scheduling, basic blocks, dependency graphs, ready lists, and critical paths",
  "sh/be103itw": "Learned to connect scheduling decisions with KITES pipeline behaviour and measured cycles",
  "sh/wvqh0vil": "Improved Claude Code and Codex skills, including using subagents and checking AI-generated work",
  "sh/03e58vu1": "Learned that unit tests, random-program checks, and manual testing find different kinds of bugs",
  "sh/ytsnypo7": "Source: project reports, implementation work, and testing experience",
};
for (const [id,value] of Object.entries(edits)) {
  const obj=p.resolve(id);
  if (!obj) throw new Error(`Missing ${id}`);
  obj.text=value;
}

const scripts = [
`Good morning. My project extends the KITES RISC-V simulator with instruction reordering and stall analysis. The main goal is to help a user understand where pipeline cycles are being wasted, suggest a safer instruction order, and then measure whether that change actually improves execution. I will first explain the scheduler and the interface, then show the results, verification, contributions, and limitations. [About 25 seconds]`,

`First, why do we need reordering? In the original order, the add immediately depends on the value loaded into x3. Since the load value is not ready yet, the add has to wait. But the addi instruction is independent, so we can place it between the load and the dependent add. That useful instruction occupies a cycle that would otherwise become a stall. The program result remains the same because the dependency analysis confirms that this move is safe. [About 40 seconds]`,

`Correctness is handled using a dependency graph. A basic block is a group of consecutive instructions with no branch or jump in the middle. Inside each block, every instruction becomes a node. The edges represent restrictions such as read after write, write after read, write after write, memory ordering, and control boundaries. The scheduler is only allowed to choose an order that respects all these edges. So it changes the schedule, but it should not change what the program computes. [About 45 seconds]`,

`The scheduling approach follows the static heuristic from Lecture 14. I first divide the program into basic blocks and build the dependency graph. Then I maintain a list of instructions whose dependencies are already satisfied. From that ready list, I prefer an instruction that avoids the next stall. If several instructions are possible, the critical path helps decide the priority. One important adaptation is that the project uses KITES' own hazard rules when estimating stalls, so the prediction follows the simulator's behaviour. [About 50 seconds]`,

`This is the main example I will demonstrate, Bubble Sort plus Sum from the lab exam. The left side shows the original code and the right side shows the suggested order. The green rows show instructions that were moved. The important feature is not only that the tool says move this instruction. It also explains why the move helps. It identifies the instruction that was waiting, shows that the moved instruction is independent, and explains that it fills a stall slot while the required value is being produced. So the user can understand the reason behind the reordering instead of blindly accepting it. The user can also select suggestions block by block, compare the original and suggested versions, and apply the change only after KITES verifies the final registers and memory. In this run, cycles reduce from 838 to 756 and data stalls reduce from 160 to 76. [About 75 seconds]`,

`This graph compares the same example with and without forwarding. With forwarding, cycles reduce from 695 to 680, which is about 2.2 percent. Without forwarding, they reduce from 838 to 756, which is about 9.8 percent. The improvement is larger without forwarding because more dependencies force the processor to wait, so there are more empty slots that independent instructions can fill. [About 40 seconds]`,

`The Stall Profiler explains where the total execution time goes. For this no-forwarding run, the program takes 838 cycles. Out of these, 497 cycles retire useful instructions. There are 158 data-stall cycles, 179 branch or jump flush cycles, and 4 pipeline fill and drain cycles. That gives 341 overhead cycles. The profiler connects these costs back to source lines and names the cause. Reordering mainly targets data stalls. It normally cannot remove branch flushes. [About 50 seconds]`,

`The main novelty is the complete connection between diagnosis and a verified code change. The tool explains the proposed move, predicts its effect using KITES' hazard behaviour, shows evidence from real simulator runs, and checks the final state before applying the change. This makes the feature useful for learning because the user sees both the performance improvement and the reason that the move is legal and helpful. [About 40 seconds]`,

`For verification, I used several levels of checking. The dependency tests check that every reordered block respects the graph. The simulator comparison checks predicted cycles against measured KITES cycles. The equivalence check compares the final registers and memory. The current headless suite reports 203 out of 203 tests passing. Many generated programs and many combinations of accepted blocks were also tested, along with manual examples. [About 45 seconds]`,

`This slide gives the current engineering footprint and the division of contribution. I studied the scheduling method, attempted the static scheduler in C++ with Codex guidance, performed manual testing, reviewed the results, and made the final project decisions. Claude and Codex assisted substantially with implementation, debugging, the UI integration, automated verification, documentation, and report drafting. The UI work was mainly AI-assisted, and I validated the behaviour and accepted the final changes. [About 45 seconds]`,

`My main technical learning was list scheduling: basic blocks, dependency graphs, ready lists, and critical paths. I also learned how scheduling choices appear as stalls and cycles inside KITES. On the development side, I improved my Claude Code and Codex skills, including how to use subagents for separate tasks and how to review AI-generated code instead of accepting it directly. I also learned that manual testing, unit tests, and generated-program checks catch different types of problems. The major challenges were KITES-specific hazard quirks, interactions between neighbouring blocks, correct classification of stalls and flushes, and keeping long profiling runs responsive. [About 55 seconds]`,

`The implementation is intentionally conservative. Reordering stays inside basic blocks. Floating-point instructions are usually treated as barriers, memory operations remain ordered, and the static model does not fully represent how often each loop executes. Possible extensions include loop-aware optimisation, safe loop unrolling, better branch analysis, floating-point scheduling after handling KITES quirks, and support for wider or dual-issue pipelines. Loop-aware optimisation is especially useful because repeated branch flushes dominate programs such as Bubble Sort. [About 45 seconds]`,

`That concludes my presentation. Thank you, and I am ready for questions. [About 10 seconds]`
];

if (scripts.length !== p.slides.items.length) throw new Error("Script count does not match slide count");
for (let i=0;i<scripts.length;i++) {
  p.slides.items[i].speakerNotes.text=scripts[i];
  p.slides.items[i].speakerNotes.setVisible(true);
}

await (await PresentationFile.exportPptx(p)).save(output);
console.log(output);
