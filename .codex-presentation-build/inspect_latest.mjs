import { FileBlob, PresentationFile } from "@oai/artifact-tool";
const file="/home/chaya_147/Downloads/PPT Making Slides/KITES_Presentation_With_Script.pptx";
const p=await PresentationFile.importPptx(await FileBlob.load(file));
for (const query of ["Highest-value next step", "Loop-aware", "7,500", "7,316 subsets", "Independent regression check"]) {
  const r=await p.inspect({kind:"textbox",search:query,maxChars:10000});
  console.log(`---${query}---\n${r.ndjson}`);
}
