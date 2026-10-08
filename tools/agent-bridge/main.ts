import { readFile } from 'node:fs/promises';
import { createConnection } from 'node:net';
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { ListToolsRequestSchema, CallToolRequestSchema } from '@modelcontextprotocol/sdk/types.js';

type Descriptor = {version:number;pipe:string;project:string;host:string;credential:string;authoring:boolean};
const path=process.argv[2];
if(!path)throw new Error('Usage: pinned-node main.ts EXPLICIT_EDITOR_DESCRIPTOR');
const file=await readFile(path);if(file.length>4096)throw new Error('Descriptor size limit');
const descriptor=JSON.parse(file.toString('utf8')) as Descriptor;
if(descriptor.version!==1 || !descriptor.pipe.startsWith('\\\\.\\pipe\\DarkAngel-') || !/^[a-f0-9]{64}$/.test(descriptor.host) || !/^[a-f0-9]{64}$/.test(descriptor.credential))throw new Error('Invalid selected host descriptor');
let requestId=0;
function native(method:string,args:Record<string,unknown>):Promise<Record<string,unknown>> {
  return new Promise((resolve,reject)=>{
    const id=String(++requestId);
    const payload=Buffer.from(JSON.stringify({jsonrpc:'2.0',id,method,params:{authorization:{project:descriptor.project,host:descriptor.host,credential:descriptor.credential},arguments:args}}));
    if(payload.length>65536){reject(new Error('Request frame limit'));return;}
    const frame=Buffer.alloc(payload.length+4);frame.writeUInt32LE(payload.length);payload.copy(frame,4);
    const socket=createConnection(descriptor.pipe);let bytes=Buffer.alloc(0),settled=false;
    const fail=(error:Error)=>{if(settled)return;settled=true;socket.destroy();reject(error);};
    socket.setTimeout(5000,()=>fail(new Error('Selected host request deadline; query operation status before retrying a mutation')));
    socket.on('error',()=>fail(new Error('Selected native host unavailable')));
    socket.on('end',()=>{if(!settled)fail(new Error('Native host disconnected before reply'));});
    socket.on('connect',()=>socket.write(frame));
    socket.on('data',chunk=>{
      bytes=Buffer.concat([bytes,chunk]);if(bytes.length>65540){fail(new Error('Response frame limit'));return;}
      if(bytes.length<4)return;const size=bytes.readUInt32LE();if(size===0 || size>65536){fail(new Error('Response size limit'));return;}if(bytes.length<size+4)return;
      try{if(bytes.length!==size+4)throw new Error('Unexpected extra reply');const response=JSON.parse(bytes.subarray(4).toString('utf8'));if(response.jsonrpc!=='2.0' || response.id!==id)throw new Error('Response correlation mismatch');if(response.error)throw new Error(String(response.error.message));if(response.result.project!==descriptor.project || response.result.host!==descriptor.host)throw new Error('Native project/host mismatch');settled=true;socket.destroy();resolve(response.result);}catch(error){fail(error as Error);}
    });
  });
}
// One selected local host and one bounded request at a time. SDK handles MCP
// lifecycle/framing. Native JSON-RPC supplies every authoring transaction.
let queue:Promise<unknown>=Promise.resolve();
function call(method:string,args:Record<string,unknown>){const result=queue.then(()=>native(method,args));queue=result.catch(()=>{});return result;}
const describe=await call('describe',{});
const data=describe.data as {catalogVersion:number;tools:Array<{name:string;description:string;inputSchema:Record<string,unknown>;authoring:boolean}>;authoring:boolean};
if(data.catalogVersion!==1)throw new Error('Unsupported native capability catalog');
const tools=data.tools.filter(t=>!t.authoring || data.authoring);
const server=new Server({name:'darkangel-editor',version:'0.0.1'},{capabilities:{tools:{}}});
server.setRequestHandler(ListToolsRequestSchema,async()=>({tools:tools.map(t=>({name:`darkangel_${t.name}`,description:t.description,inputSchema:t.inputSchema as never,annotations:{readOnlyHint:!t.authoring,destructiveHint:false,openWorldHint:false}}))}));
server.setRequestHandler(CallToolRequestSchema,async request=>{
  const tool=tools.find(t=>`darkangel_${t.name}`===request.params.name);if(!tool)return {isError:true,content:[{type:'text',text:'Unknown curated tool'}]};
  try{const result=await call(tool.name,request.params.arguments ?? {});return {structuredContent:result,content:[{type:'text',text:JSON.stringify(result)}]};}
  catch(error){return {isError:true,content:[{type:'text',text:(error as Error).message}]};}
});
await server.connect(new StdioServerTransport());
