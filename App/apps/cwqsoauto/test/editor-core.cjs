"use strict";
// No dependencies: tests execute the exact core embedded in editor.html.
const fs=require("node:fs"),path=require("node:path"),vm=require("node:vm"),assert=require("node:assert/strict");
const {webcrypto}=require("node:crypto");
const root=path.resolve(__dirname,".."),out=path.join(__dirname,"out-editor");fs.mkdirSync(out,{recursive:true});
const html=fs.readFileSync(path.join(root,"editor.html"),"utf8");
const source=html.match(/<script id="bank16-core">([\s\S]*?)<\/script>/)[1];
const context=vm.createContext({crypto:webcrypto,Uint8Array,ArrayBuffer,DataView});vm.runInContext(source,context);
const core=context.Bank16;
const PROFILE=core.PROFILES.find(p=>p.id==="names16-selected");assert.ok(PROFILE);
const reference=new Uint8Array(fs.readFileSync(path.join(root,"CWQSOAuto.app")));
const start=64+PROFILE.codeSize;
const clone=()=>reference.slice();
const assetsCRC=b=>new DataView(b.buffer).setUint16(62,core.crc32(b.subarray(start))&65535,true);
const codeCRC=b=>new DataView(b.buffer).setUint32(12,core.crc32(b.subarray(64,start)),true);
let groups=0,corruptions=0;
async function test(name,fn){await fn();groups++;console.log("PASS",name);}
async function rejected(bytes,pattern){await assert.rejects(()=>core.validateFile(bytes),pattern);}
(async()=>{
  const originalHash=Buffer.from(reference).toString("hex");
  const embedded=Buffer.from(html.match(/id="bank16-reference"[^>]*>([\s\S]*?)<\/script>/)[1],"base64");
  await test("Référence embarquée identique au APP livré",()=>assert.deepEqual(embedded,Buffer.from(reference)));
  const parsed=await core.validateFile(reference);
  await test("FAP1 valide, 16 messages et valeurs initiales",()=>{
    assert.equal(parsed.messages.length,16);assert.equal(parsed.messages[0],"CQ CQ CQ DE F4XXX F4XXX K");
    assert.equal(parsed.messages[1],"DE F4XXX UR {RST} {RST} NAME SEB HW?");
    assert.equal(parsed.messages[14],"AGN?");assert.equal(parsed.messages[15],"?");
  });
  await test("Génération sans modification strictement identique",async()=>{
    const result=await core.generate(reference,Array.from(parsed.messages));assert.deepEqual(result.bytes,reference);assert.equal(result.changed,0);
  });
  await test("Modification indépendante de chacun des 16 messages",async()=>{
    for(let i=0;i<16;i++){
      const texts=Array.from(parsed.messages);texts[i]=`TEST ${i+1} {RST}`;
      const result=await core.generate(reference,texts),decoded=await core.validateFile(result.bytes);
      assert.deepEqual(Array.from(decoded.messages),texts);assert.ok(result.changed>0);
      for(let n=0;n<16;n++)if(n!==i){
        const off=start+512+n*68;assert.deepEqual(result.bytes.subarray(off,off+68),reference.subarray(off,off+68));
      }
      core.compareAllowed(reference,result.bytes,PROFILE);
    }
  });
  await test("Noms indépendants, courts, vides et limite 12 pour les 16 messages",async()=>{
    assert.equal(parsed.names.length,16);
    for(let i=0;i<16;i++)for(const name of ["", "POTA", "ABCDEFGHIJKL"]){
      const names=Array.from(parsed.names);names[i]=name;
      const output=await core.generate(reference,Array.from(parsed.messages),names);
      const reopened=await core.validateFile(output.bytes);
      assert.deepEqual(Array.from(reopened.names),names);
      assert.deepEqual(Array.from(reopened.messages),Array.from(parsed.messages));
      for(let j=0;j<16;j++)if(j!==i)assert.equal(reopened.names[j],parsed.names[j]);
      assert.deepEqual(output.bytes.subarray(64,start),reference.subarray(64,start));
    }
  });
  await test("Noms : ASCII exact, longueurs et métadonnées immuables",async()=>{
    for(let c=0;c<256;c++){
      if(c>=32 && c<=126)assert.equal(core.encodeName(String.fromCharCode(c),1)[0],c);
      else assert.throws(()=>core.encodeName(String.fromCharCode(c),1),/nom/);
    }
    for(const invalid of ["A".repeat(13),"é","😀","\0","\n"]){
      const names=Array.from(parsed.names);names[15]=invalid;
      await assert.rejects(()=>core.generate(reference,Array.from(parsed.messages),names),/MSG 16/);
    }
    for(let i=0;i<16;i++)for(let j=0;j<3;j++){
      const b=clone();b[start+PROFILE.nameBase+i*16+j]^=1;assetsCRC(b);await rejected(b);
      assert.throws(()=>core.compareAllowed(reference,b,PROFILE),/interdite/);
    }
    for(const [at,value] of [[3,255],[15,65],[3,0],[8,1]]){
      const b=clone();b[start+PROFILE.nameBase+at]=value;assetsCRC(b);await rejected(b,/MSG 01/);
    }
    const b=clone();b[start+PROFILE.marker]^=1;assetsCRC(b);await rejected(b,/QNM1/);
  });
  await test("16 noms et contenus modifiés ensemble, export puis réouverture",async()=>{
    const names=Array.from({length:16},(_,i)=>`POTA ${i+1}`);
    const texts=Array.from({length:16},(_,i)=>`E {RST} ${i+1}`);
    const output=await core.generate(reference,texts,names),r=await core.validateFile(output.bytes);
    assert.deepEqual(Array.from(r.names),names);assert.deepEqual(Array.from(r.messages),texts);
    fs.writeFileSync(path.join(out,"named-custom.app"),output.bytes);
  });
  await test("Anciens fichiers : contenus préservés, absence de noms explicite",async()=>{
    for(const file of [path.resolve(root,"../../../archive/apps/v6.3.0/CWQSOAuto.app")]){
      const old=new Uint8Array(fs.readFileSync(file)),r=await core.validateFile(old);
      assert.equal(r.names,null);
      assert.deepEqual((await core.generate(old,Array.from(r.messages))).bytes,old);
      await assert.rejects(()=>core.generate(old,Array.from(r.messages),Array(16).fill("X")),/Ancien format sans noms/);
    }
  });
  if(process.env.CWQSO_NAMES_REFERENCE)await test("Ancienne référence nommée : noms, contenus et code préservés",async()=>{
    const old=new Uint8Array(fs.readFileSync(process.env.CWQSO_NAMES_REFERENCE));
    const r=await core.validateFile(old);assert.equal(r.profile.id,"names16");assert.equal(r.names.length,16);
    const output=await core.generate(old,Array.from(r.messages),Array.from(r.names));
    assert.deepEqual(output.bytes,old);
  });
  await test("64 octets utiles, sans NUL ; 64 jetons RST = 64 octets",async()=>{
    for(const text of ["A".repeat(64),"{RST}".repeat(64),"A".repeat(63)+"{RST}"]){
      assert.equal(core.encodeMessage(text,1).length,64);
      const texts=Array.from(parsed.messages);texts[0]=text;
      assert.equal((await core.validateFile((await core.generate(reference,texts)).bytes)).messages[0],text);
    }
  });
  await test("65 octets refusés sans troncature",async()=>{
    for(const text of ["A".repeat(65),"{RST}".repeat(65),"A".repeat(64)+"{RST}"]){
      assert.throws(()=>core.encodeMessage(text,6),/MSG 06.*65 octets/);
      const texts=Array.from(parsed.messages);texts[5]=text;await assert.rejects(()=>core.generate(reference,texts));
    }
  });
  await test("Alphabet exact, Unicode et contrôles refusés avec numéro et caractère",()=>{
    for(let c=0;c<256;c++){
      const character=String.fromCharCode(c),text=`A${character}A`;
      if(core.ALPHABET.includes(character))assert.equal(core.encodeMessage(text,7).length,3);
      else assert.throws(()=>core.encodeMessage(text,7),/MSG 07.*caractère/);
    }
    for(const bad of ["é","😀","\ud800","\u00a0","Ａ","\u202e","\n","\r","\t","\0","\x02",".",",","-","a","{CALL}","{rst}","{RST", "}"])
      assert.throws(()=>core.encodeMessage(bad,12),/MSG 12/);
    assert.deepEqual(Array.from(core.encodeMessage("A{RST} B? /",2)),[65,2,32,66,63,32,47]);
  });
  await test("Espaces invalides refusés ; message vide légal",async()=>{
    for(const text of [" A","A ","A  A"," "])assert.throws(()=>core.encodeMessage(text,3),/MSG 03.*espace/);
    const texts=Array.from(parsed.messages);texts[0]="";const r=await core.generate(reference,texts);
    assert.equal((await core.validateFile(r.bytes)).messages[0],"");
    assert.ok(r.bytes.subarray(start+516,start+580).every(b=>b===0));
  });
  await test("Fichiers tronqués, ajoutés, autres tailles et types refusés",async()=>{
    for(const n of [0,1,63,64,512,reference.length-1])await rejected(reference.slice(0,n));
    await rejected(new Uint8Array(8001));const extra=new Uint8Array(reference.length+1);extra.set(reference);await rejected(extra);
    await rejected("FAP1");await rejected(new DataView(reference.buffer));
  });
  await test("Tous les champs d’en-tête incompatibles sont refusés",async()=>{
    for(const off of [0,4,6,7,8,16,18,20,31,36,40,52,56,60]){const b=clone();b[off]^=1;await rejected(b);}
  });
  await test("Code altéré refusé même avec CRC recalculé",async()=>{
    const b=clone();b[100]^=1;codeCRC(b);await rejected(b,/Code exécutable/);
  });
  await test("Autres assets altérés refusés même avec CRC recalculé",async()=>{
    for(const off of [0,46,47,91,403,1600,2200]){const b=clone();b[start+off]^=1;assetsCRC(b);await rejected(b,/Assets hors banque/);}
  });
  await test("Toutes les signatures et métadonnées de banque contrôlées",async()=>{
    for(let i=0;i<16;i++)for(let j=0;j<4;j++){
      const b=clone();b[start+512+i*68+j]^=1;assetsCRC(b);await rejected(b,/signature/);
    }
  });
  await test("Textes binaires et padding invalides refusés avec CRC valide",async()=>{
    for(const byte of [1,3,10,46,97,128,255]){const b=clone();b[start+516]=byte;assetsCRC(b);await rejected(b,/MSG 01/);}
    const b=clone();b[start+516+26]=65;assetsCRC(b);await rejected(b,/remplissage/);
  });
  await test("Comparateur bloque tout changement hors données et CRC assets",()=>{
    for(const off of [0,12,64,100,start,start+512,start+515,start+1600,reference.length-1]){
      const b=clone();b[off]^=1;assert.throws(()=>core.compareAllowed(reference,b,PROFILE),/interdite/);
    }
    assert.throws(()=>core.compareAllowed(reference,new Uint8Array(1),PROFILE),/longueur/);
  });
  await test("Une corruption individuelle à chaque octet du fichier est refusée",async()=>{
    for(let i=0;i<reference.length;i++){const b=clone();b[i]^=1;await rejected(b);corruptions++;}
  });
  await test("Snapshots indépendants ; original jamais modifié",async()=>{
    const data=reference.buffer.slice(0);const p=await core.validateFile(data);
    new Uint8Array(data)[100]^=1;assert.equal(p.bytes[100],reference[100]);
    assert.equal(Buffer.from(reference).toString("hex"),originalHash);
    await assert.rejects(()=>core.generate(reference,[]),/16 messages/);
  });
  await test("Vérification indisponible = refus fermé",async()=>{
    const noCrypto=vm.createContext({Uint8Array,ArrayBuffer,DataView});vm.runInContext(source,noCrypto);
    await assert.rejects(()=>noCrypto.Bank16.validateFile(reference),/SHA-256 indisponible/);
  });
  await test("Exemple réservé MSG 04 et rechargement du résultat",async()=>{
    const texts=Array.from(parsed.messages);texts[3]="TEST WEB {RST}";
    const result=await core.generate(reference,texts);
    assert.equal((await core.validateFile(result.bytes)).messages[3],texts[3]);
    assert.deepEqual(result.bytes.subarray(64,start),reference.subarray(64,start));
    assert.deepEqual(result.bytes.subarray(12,16),reference.subarray(12,16));
    fs.writeFileSync(path.join(out,"example.app"),result.bytes);
    fs.writeFileSync(path.join(out,"example-result.json"),JSON.stringify({changed:result.changed,messages:result.messages},null,2));
  });
  const result={groups,corruptions,independent_record_edits:16,signature_tamper_cases:64,all_passed:true};
  fs.writeFileSync(path.join(out,"core-results.json"),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
})().catch(error=>{console.error(error);process.exitCode=1;});
