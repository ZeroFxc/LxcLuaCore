#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static HANDLE hWr, hRd;
static void send_msg(const char *msg) {
    int len = (int)strlen(msg);
    char hdr[128]; int hl = snprintf(hdr, sizeof(hdr), "Content-Length: %d\r\n\r\n", len);
    DWORD w; WriteFile(hWr, hdr, hl, &w, NULL); WriteFile(hWr, msg, len, &w, NULL);
}
static char *read_msg(void) {
    char buf[4096]; DWORD t=0;
    while(t<sizeof(buf)-1){DWORD n;if(!ReadFile(hRd,buf+t,1,&n,NULL)||n==0)return NULL;t++;
    if(t>=4&&buf[t-4]=='\r'&&buf[t-3]=='\n'&&buf[t-2]=='\r'&&buf[t-1]=='\n')break;}buf[t]=0;
    int cl=0; const char *p=strstr(buf,"Content-Length:");if(p)cl=atoi(p+15);if(cl<=0)return NULL;
    char *b=malloc(cl+1);DWORD rt=0;while(rt<(DWORD)cl){DWORD n;ReadFile(hRd,b+rt,cl-rt,&n,NULL);rt+=n;}b[cl]=0;return b;
}
int main(){
    HANDLE ri,wo; SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    CreatePipe(&ri,&hWr,&sa,0);CreatePipe(&hRd,&wo,&sa,0);
    SetHandleInformation(hWr,HANDLE_FLAG_INHERIT,0);SetHandleInformation(hRd,HANDLE_FLAG_INHERIT,0);
    PROCESS_INFORMATION pi={0};STARTUPINFOA si={0};si.cb=sizeof(si);
    si.hStdInput=ri;si.hStdOutput=wo;si.hStdError=GetStdHandle(STD_ERROR_HANDLE);si.dwFlags=STARTF_USESTDHANDLES;
    CreateProcessA(NULL,"lxclua-lsp.exe",NULL,NULL,TRUE,0,NULL,NULL,&si,&pi);
    CloseHandle(ri);CloseHandle(wo);Sleep(200);
    char *r;
    
    /* Test code */
    const char *code="local x=1\nlocal y:int=2\nlet z=3\nconst W=4\nexport function f()end\nfunction g()\n  if true then\n    print(1)\n  end\nend\nstruct Point x,y end\n";

    /* Init */
    printf("=== Init ===\n");
    send_msg("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"processId\":1,\"rootUri\":\"file:///t\",\"capabilities\":{}}}");
    r=read_msg();printf("%s\n\n",r?r:"NULL");free(r);

    printf("=== Initialized ===\n");
    send_msg("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");

    printf("=== didOpen ===\n");
    char ob[4096];char *e=malloc(strlen(code)*2);char *p=e;
    for(const char *s=code;*s;s++){if(*s=='\n'){*p++='\\';*p++='n';}else if(*s=='"'){*p++='\\';*p++='"';}else *p++=*s;}*p=0;
    snprintf(ob,sizeof(ob),"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file:///t/m.lua\",\"languageId\":\"lxclua\",\"version\":1,\"text\":\"%s\"}}}",e);
    free(e);send_msg(ob);

    /* Test each new method individually */
    #define TEST(id,method,params) do{ \
        printf("=== %s ===\n",method); \
        send_msg("{\"jsonrpc\":\"2.0\",\"id\":" #id ",\"method\":\"" method "\",\"params\":" params "}"); \
        r=read_msg(); printf("%.800s\n\n",r?r:"NULL [no response]"); free(r); \
        Sleep(100); \
    }while(0)

    TEST(2,"textDocument/completion","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":0,\"character\":2}}");
    TEST(3,"textDocument/documentSymbol","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"}}");
    TEST(4,"textDocument/foldingRange","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"}}");
    TEST(5,"textDocument/semanticTokens/full","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"}}");
    TEST(6,"textDocument/codeAction","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},\"context\":{\"diagnostics\":[]}}");
    TEST(7,"textDocument/formatting","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"options\":{\"tabSize\":4,\"insertSpaces\":true}}");
    TEST(8,"textDocument/diagnostic","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"}}");
    TEST(9,"textDocument/hover","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":0,\"character\":6}}");
    TEST(10,"textDocument/definition","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":0,\"character\":6}}");
    TEST(11,"textDocument/references","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":0,\"character\":6},\"context\":{\"includeDeclaration\":true}}");
    TEST(12,"textDocument/signatureHelp","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":0,\"character\":10}}");
    TEST(13,"textDocument/documentHighlight","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":0,\"character\":6}}");
    TEST(14,"textDocument/prepareRename","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":0,\"character\":6}}");
    TEST(15,"textDocument/typeDefinition","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":0,\"character\":6}}");
    TEST(16,"textDocument/implementation","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":4,\"character\":15}}");
    TEST(17,"workspace/symbol","{\"query\":\"Point\"}");
    TEST(18,"textDocument/selectionRange","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"positions\":[{\"line\":5,\"character\":2}]}");
    TEST(19,"completionItem/resolve","{\"label\":\"print\",\"kind\":3}");
    TEST(20,"textDocument/linkedEditingRange","{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"position\":{\"line\":0,\"character\":6}}");

    /* didSave */
    printf("=== didSave ===\n");
    send_msg("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{\"textDocument\":{\"uri\":\"file:///t/m.lua\"}}}");
    printf("OK (notification)\n\n");

    /* workspace config */
    printf("=== workspace/didChangeConfiguration ===\n");
    send_msg("{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeConfiguration\",\"params\":{\"settings\":{}}}");
    printf("OK (notification)\n\n");

    /* Shutdown */
    printf("=== Shutdown ===\n");
    send_msg("{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"shutdown\",\"params\":{}}");
    r=read_msg();printf("%s\n\n",r?r:"NULL");free(r);
    send_msg("{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}");
    WaitForSingleObject(pi.hProcess,3000);
    CloseHandle(hWr);CloseHandle(hRd);CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
    printf("Done.\n");return 0;
}