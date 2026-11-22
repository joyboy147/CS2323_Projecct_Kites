import { FileBlob, PresentationFile } from "@oai/artifact-tool";
const source = "/home/chaya_147/Downloads/PPT Making Slides/KITES_Final_Presentation_Clarified.pptx";
const output = "/home/chaya_147/Downloads/PPT Making Slides/KITES_Final_Presentation_Clarified_v2.pptx";
const p = await PresentationFile.importPptx(await FileBlob.load(source));
const edits = {
  "sh/i1onq5gv": "selected scheduler and profiler test files",
  "sh/2503250f": "reported current headless suite",
  "sh/8v2t0rep": "Source: current project tests and verification report",
  "sh/yhkvyhs3": "Source: current repository snapshot and project reports",
};
for (const [id,value] of Object.entries(edits)) p.resolve(id).text = value;
await (await PresentationFile.exportPptx(p)).save(output);
console.log(output);
