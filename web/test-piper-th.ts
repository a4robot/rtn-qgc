import * as tts from '@mintplex-labs/piper-tts-web';

async function main() {
  try {
    const wav = await tts.predict({
      text: "สวัสดีครับ",
      voiceId: 'th_TH-natten-medium'
    });
    console.log("Success! size:", wav.size);
  } catch (e) {
    console.error("Failed:", e.message);
  }
}
main().catch(console.error);
