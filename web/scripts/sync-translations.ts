import { readFileSync, readdirSync, writeFileSync, mkdirSync, existsSync } from "fs";
import { join } from "path";

const translationsDir = join(import.meta.dir, "../../translations");
const localesDir = join(import.meta.dir, "../src/locales");

// regex to match <message> block
const messageRegex = /<message>([\s\S]*?)<\/message>/g;
const sourceRegex = /<source>([\s\S]*?)<\/source>/;
const translationRegex = /<translation[^>]*>([\s\S]*?)<\/translation>/;

function unescapeXml(unsafe: string) {
  return unsafe
    .replace(/&amp;/g, "&")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#x27;/g, "'");
}

function processFile(file: string, lang: string) {
  const content = readFileSync(join(translationsDir, file), "utf8");
  const dict: Record<string, string> = {};
  
  let match;
  while ((match = messageRegex.exec(content)) !== null) {
    const messageBlock = match[1];
    const sourceMatch = sourceRegex.exec(messageBlock);
    const translationMatch = translationRegex.exec(messageBlock);
    
    if (sourceMatch && translationMatch) {
      const source = unescapeXml(sourceMatch[1]);
      const translation = unescapeXml(translationMatch[1]);
      
      // Skip unfinished or vanished translations, and empty strings
      if (translation && !messageBlock.includes('type="unfinished"') && !messageBlock.includes('type="vanished"')) {
        dict[source] = translation;
      }
    }
  }

  // Create locale dir if it doesn't exist
  const langDir = join(localesDir, lang);
  if (!existsSync(langDir)) {
    mkdirSync(langDir, { recursive: true });
  }

  // Write crowdin.json
  const outPath = join(langDir, "crowdin.json");
  writeFileSync(outPath, JSON.stringify(dict, null, 2), "utf8");
  console.log(`Synced ${Object.keys(dict).length} strings from ${file} -> locales/${lang}/crowdin.json`);
}

function main() {
  if (!existsSync(translationsDir)) {
    console.error("Translations directory not found at " + translationsDir);
    process.exit(1);
  }

  const files = readdirSync(translationsDir).filter(f => f.startsWith("qgc_source_") && f.endsWith(".ts"));
  
  console.log(`Found ${files.length} translation files to sync...`);

  for (const file of files) {
    // Extract language code, e.g. qgc_source_es_ES.ts -> es
    // Or qgc_source_zh_CN.ts -> zh-CN
    const langMatch = file.match(/qgc_source_([a-z]+(?:_[a-zA-Z]+)?)\.ts/);
    if (!langMatch) continue;
    
    // Convert underscore to dash for standard locale code (e.g. zh_CN -> zh-CN)
    // But for simplicity, we can just take the first part (e.g. es_ES -> es) to match our UI selection
    const rawLang = langMatch[1];
    const shortLang = rawLang.split('_')[0]; 
    
    processFile(file, shortLang);
  }

  console.log("Done! You can now import these JSON files in web/src/i18n.ts");
}

main();
