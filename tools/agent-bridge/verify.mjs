import { spawn } from 'node:child_process';
import { readFile, rm } from 'node:fs/promises';
import { setTimeout as pause } from 'node:timers/promises';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { resolve } from 'node:path';
const root=resolve(import.meta.dirname,'../..');const hostPath=process.argv[2] ?? resolve(root,'build/m1-debug/AgentHost.exe');
const descriptor=resolve(root,`.cache/editor/agent-acceptance-${process.pid}-${Date.now()}.json`);
const editor=process.argv[3]==='--editor';
const model=editor?JSON.parse(await readFile(resolve(root,'.cache/fixtures/horned-mask.glb.daimport'),'utf8')).id:null;
const args=editor?['--registry',resolve(root,'.cache/fixtures/mask.registry.json'),'--cas',resolve(root,'.cache/assets-real/cas'),'--model',model,'--frames','3000','--hidden','--agent-project','11111111111141118111111111111111','--agent-descriptor',descriptor,'--agent-authoring','--capture',resolve(root,'docs/implementation/evidence/m3-editor-agent-live.png')]:[resolve(root,'.cache/editor/AcceptanceScene.dascene'),'11111111111141118111111111111111',descriptor,'--authoring'];
const host=spawn(hostPath,args,{stdio:['ignore','ignore','pipe'],windowsHide:true});
let stderr='';host.stderr.on('data',data=>stderr+=data.toString());let transport;let client;
function assert(ok,message){if(!ok)throw new Error(message);}
try{
  let found=false;for(let i=0;i<500;i++){try{await readFile(descriptor);found=true;break;}catch{await pause(20);}}assert(found,`Host startup failed: ${stderr}`);
  transport=new StdioClientTransport({command:process.execPath,args:[resolve(root,'tools/agent-bridge/main.ts'),descriptor],cwd:root,stderr:'pipe',maxBufferSize:131072});
  transport.stderr?.on('data',data=>stderr+=data.toString());client=new Client({name:'darkangel-acceptance',version:'1.0.0'});
  const request=client.request.bind(client);let negotiatedVersion;
  client.request=async(...args)=>{const result=await request(...args);if(args[0].method==='initialize')negotiatedVersion=result.protocolVersion;return result;};
  await client.connect(transport);assert(negotiatedVersion==='2025-11-25','Pinned MCP compatibility profile changed');console.log(`MCP negotiated compatibility profile ${negotiatedVersion}`);
  const tools=await client.listTools();assert(tools.tools.length===8,'Native catalog differs from MCP tools');
  const call=async(name,args)=>{const result=await client.callTool({name:`darkangel_${name}`,arguments:args});assert(!result.isError,JSON.stringify(result));return result.structuredContent;};
  const describe=await call('describe',{});assert(describe.data.authoring,'Authoring session missing');const before=await call('inspect',{revision:describe.revision,offset:'0'});const object=before.data.entities[0].object;const yaw=before.data.entities[0].transform.yaw;
  const plan=await call('prepare',{revision:before.revision,scope:'placement',changes:[{object,type:1,property:1,value:yaw+0.25}]});
  const commit={plan:plan.data.plan,digest:plan.data.digest,operation:'00000000000000000000000000000001'};const applied=await call('commit',commit);const retried=await call('commit',commit);assert(applied.data.revision===retried.data.revision,'MCP retry changed revision');
  const after=await call('inspect',{revision:applied.data.revision,offset:'0'});assert(after.data.entities[0].transform.yaw===yaw+0.25,'MCP edit did not change native document');
  const undone=await call('undo',{revision:applied.data.revision,head:applied.historyHead.token,operation:'00000000000000000000000000000002'});const restored=await call('inspect',{revision:undone.data.revision,offset:'0'});assert(restored.data.entities[0].transform.yaw===yaw,'MCP shared undo failed');
  console.log('M3 real current-user pipe / official SDK stdio / catalog / prepare / commit / retry / undo passed');
  if(editor){await client.close();await transport.close();await new Promise((resolve,reject)=>{if(host.exitCode!==null){host.exitCode===0?resolve():reject(new Error('Editor failed'));return;}const timeout=setTimeout(()=>reject(new Error('Editor capture deadline')),30000);host.once('exit',code=>{clearTimeout(timeout);code===0?resolve():reject(new Error('Editor failed'));});});await readFile(resolve(root,'docs/implementation/evidence/m3-editor-agent-live.png'));console.log('Native editor issued scoped endpoint, reflected MCP transaction/history, and captured final UI');}
}finally{await client?.close();await transport?.close();if(host.exitCode===null){host.kill();await new Promise(resolve=>host.once('exit',resolve));}await rm(descriptor,{force:true});}
