import { readFileSync, writeFileSync, existsSync } from "fs";
import { join } from "path";
import { $ } from "bun";

const translationsDir = join(import.meta.dir, "../../translations");
const qgcTsPath = join(translationsDir, "qgc.ts");
const outTsPath = join(translationsDir, "qgc_source_th_TH.ts");

const messageRegex = /<message>([\s\S]*?)<\/message>/g;
const sourceRegex = /<source>([\s\S]*?)<\/source>/;

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

async function translateBatch(sources: string[]): Promise<string[]> {
  const promptText = `You are an expert translator for a Drone Ground Control Station (QGroundControl).
Translate the following English strings to Thai. 
Rules:
1. Maintain all special placeholders like %1, %2, %n, <br>, etc. exactly as they are.
2. Keep technical terms natural for drone pilots (e.g., Roll, Pitch, Yaw, Waypoint, Armed).
3. Return ONLY a valid JSON array of strings in the exact same order. Do not wrap in markdown, do not use tools, do not output any other conversational text.

Strings to translate:
${JSON.stringify(sources, null, 2)}`;

  // Use headless agy instead of external API
  const { stdout, stderr, exitCode } = await $`agy -p ${promptText}`.quiet();

  if (exitCode !== 0) {
    throw new Error(`agy CLI Error: ${stderr.toString()}`);
  }

  const content = stdout.toString().trim();
  
  // Clean markdown block if the AI returned it
  const jsonStr = content.replace(/^```json/, "").replace(/```$/, "").trim();
  
  try {
    const translated = JSON.parse(jsonStr);
    if (!Array.isArray(translated) || translated.length !== sources.length) {
      throw new Error("Returned array length mismatch.");
    }
    return translated;
  } catch (e) {
    console.error("Failed to parse JSON response:", jsonStr);
    throw e;
  }
}

async function main() {
  if (!existsSync(qgcTsPath)) {
    console.error(`Base file not found: ${qgcTsPath}`);
    process.exit(1);
  }

  console.log("📖 Reading qgc.ts...");
  const content = readFileSync(qgcTsPath, "utf8");
  
  // Change language header for the new file
  let newContent = content.replace(/language="en"/, 'language="th-TH" sourcelanguage="en"');

  const blocks: string[] = [];
  const sourcesToTranslate: string[] = [];
  
  let match;
  while ((match = messageRegex.exec(content)) !== null) {
    blocks.push(match[0]);
    const sourceMatch = sourceRegex.exec(match[0]);
    if (sourceMatch) {
      sourcesToTranslate.push(sourceMatch[1]);
    } else {
      sourcesToTranslate.push("");
    }
  }

  console.log(`🔍 Found ${sourcesToTranslate.filter(s => s).length} strings to translate.`);
  console.log("🚀 Starting AI Translation (Batch size: 50)...");

  const BATCH_SIZE = 50;
  const translatedSources: string[] = [];

  for (let i = 0; i < sourcesToTranslate.length; i += BATCH_SIZE) {
    const batch = sourcesToTranslate.slice(i, i + BATCH_SIZE);
    
    // Only translate non-empty strings
    const validBatch = batch.filter(s => s !== "");
    let translatedValidBatch: string[] = [];

    if (validBatch.length > 0) {
      console.log(`⏳ Translating batch ${Math.floor(i / BATCH_SIZE) + 1}/${Math.ceil(sourcesToTranslate.length / BATCH_SIZE)}...`);
      let retries = 3;
      while (retries > 0) {
        try {
          translatedValidBatch = await translateBatch(validBatch);
          break;
        } catch (e) {
          console.error(`⚠️ Batch failed. Retries left: ${retries - 1}. Error:`, (e as Error).message);
          retries--;
          await sleep(2000);
          if (retries === 0) throw e;
        }
      }
    }

    // Reconstruct batch with empty strings preserved
    let validIdx = 0;
    for (const source of batch) {
      if (source === "") {
        translatedSources.push("");
      } else {
        translatedSources.push(translatedValidBatch[validIdx]);
        validIdx++;
      }
    }
    
    // Wait a bit to avoid rate limits
    await sleep(1000);
  }

  console.log("✅ Translation complete! Reconstructing XML...");

  let outXml = newContent;
  for (let i = 0; i < blocks.length; i++) {
    const block = blocks[i];
    const source = sourcesToTranslate[i];
    const translation = translatedSources[i];
    
    if (source && translation) {
      const newBlock = block.replace(/(<\/source>)/, `$1\n        <translation>${translation}</translation>`);
      outXml = outXml.replace(block, newBlock);
    }
  }

  writeFileSync(outTsPath, outXml, "utf8");
  console.log(`🎉 Saved complete Thai translation to: ${outTsPath}`);
  console.log(`💡 Now you can run: bun run sync-i18n`);
}

main().catch(console.error);
