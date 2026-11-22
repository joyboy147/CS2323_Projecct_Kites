import { FileBlob, PresentationFile } from "@oai/artifact-tool";
const file = "/home/chaya_147/Downloads/PPT Making Slides/KITES_Final_Presentation.pptx";
const p = await PresentationFile.importPptx(await FileBlob.load(file));
const result = await p.inspect({kind:"textbox",search:"src, tests, docs, examples|testing tools and Qt stand-ins|Source: Phase 3 report",maxChars:10000});
console.log(result.ndjson);
