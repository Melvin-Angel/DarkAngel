import { spawn } from 'node:child_process';
import { readFile, rm } from 'node:fs/promises';
import { setTimeout as pause } from 'node:timers/promises';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { resolve } from 'node:path';
const root=resolve(import.meta.dirname,'../..');const hostPath=process.argv[2] ?? resolve(root,'build/m1-debug/AgentHost.exe');
const descriptor=resolve(root,'.cache/editor/agent-acceptance.json');
await rm(descriptor,{force:true});
const host=spawn(hostPath,[resolve(root,'.cache/editor/AcceptanceScene.dascene'),'11111111111141118111111111111111',descriptor,'--authoring'],{stdio:['ignore','ignore','pipe'],windowsHide:true});
let stderr='';host.stderr.on('data',data=>stderr+=data.toString());let transport;let client;
function assert(ok,message){if(!ok)throw new Error(message);}
try{
  let found=false;for(let i=0;i<100;i++){try{await readFile(descriptor);found=true;break;}catch{await pause(20);}}assert(found,`Host startup failed: ${stderr}`);
  transport=new StdioClientTransport({command:process.execPath,args:[resolve(root,'tools/agent-bridge/main.ts'),descriptor],cwd:root,stderr:'pipe',maxBufferSize:131072});
  transport.stderr?.on('data',data=>stderr+=data.toString());client=new Client({name:'darkangel-acceptance',version:'1.0.0'});await client.connect(transport);
  const tools=await client.listTools();assert(tools.tools.length===8,'Native catalog differs from MCP tools');
  const call=async(name,args)=>{const result=await client.callTool({name:`darkangel_${name}`,arguments:args});assert(!result.isError,JSON.stringify(result));return result.structuredContent;};
  const describe=await call('describe',{});assert(describe.data.authoring,'Authoring session missing');const before=await call('inspect',{revision:describe.revision,offset:'0'});const object=before.data.entities[0].object;const yaw=before.data.entities[0].transform.yaw;
  const plan=await call('prepare',{revision:before.revision,scope:'placement',changes:[{object,type:1,property:1,value:yaw+0.25}]});
  const commit={plan:plan.data.plan,digest:plan.data.digest,operation:'00000000000000000000000000000001'};const applied=await call('commit',commit);const retried=await call('commit',commit);assert(applied.data.revision===retried.data.revision,'MCP retry changed revision');
  const after=await call('inspect',{revision:applied.data.revision,offset:'0'});assert(after.data.entities[0].transform.yaw===yaw+0.25,'MCP edit did not change native document');
  const undone=await call('undo',{revision:applied.data.revision,operation:'00000000000000000000000000000002'});const restored=await call('inspect',{revision:undone.data.revision,offset:'0'});assert(restored.data.entities[0].transform.yaw===yaw,'MCP shared undo failed');
  console.log('M3 real current-user pipe / official SDK stdio / catalog / prepare / commit / retry / undo passed');
}finally{await client?.close();await transport?.close();host.kill();await new Promise(resolve=>host.once('exit',resolve));await rm(descriptor,{force:true});}
