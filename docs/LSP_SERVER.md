# LXCLUA-NCore LSP 语言服务器文档

## 1. 概述

LXCLUA-NCore 内置了 **LSP（Language Server Protocol）** 语言服务器，为 IDE 和编辑器提供智能代码辅助功能。服务器位于 `src/lspsrv/` 目录下，采用纯 C 语言实现，不依赖任何外部 JSON 库，完全自包含。

### 核心特性

- 基于 **JSON-RPC 2.0** 协议，通过 **stdio** 进行通信
- 支持 **LXCLUA 扩展语法**（类、接口、枚举、命名空间、模式匹配等）
- 全量文档同步（Full Document Sync）
- 实时诊断（语法错误、未使用变量、未定义变量等）
- 代码补全、悬停提示、定义跳转、引用查找、重命名等核心 IDE 功能
- 语义高亮、折叠范围、代码操作、调用层次结构等高级功能

---

## 2. 架构概览

LSP 服务器由 **10 个核心模块** 组成，各模块职责清晰、耦合度低。

```
┌─────────────────────────────────────────────────────────────────────┐
│                          lspsrv_main.c                              │
│                    主入口 · 消息循环 · stdio 通信                      │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         lspsrv_proto.c                              │
│                 协议路由 · 请求分发 · 响应构建 · 初始化                  │
└───────┬──────────┬──────────┬──────────┬──────────┬─────────────────┘
        │          │          │          │          │
        ▼          ▼          ▼          ▼          ▼
┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐
│lspsrv_   │ │lspsrv_   │ │lspsrv_   │ │lspsrv_   │ │lspsrv_       │
│json.c    │ │doc.c     │ │lexer.c   │ │kwdb.c    │ │complete.c    │
│JSON-RPC  │ │文档管理  │ │词法分析  │ │关键字库  │ │自动补全      │
│编解码    │ │符号表    │ │Token流   │ │标准库    │ │              │
└──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────────┘
        │          │          │          │          │
        └──────────┴──────────┴──────────┴──────────┘
                          │
        ┌─────────────────┼─────────────────┐
        ▼                 ▼                 ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────────┐
│lspsrv_       │ │lspsrv_       │ │lspsrv_           │
│hover.c       │ │features.c    │ │util.c            │
│悬停提示      │ │诊断 · 格式化 │ │通用工具函数      │
│定义跳转      │ │代码操作 · 等 │ │内存/字符串/坐标  │
└──────────────┘ └──────────────┘ └──────────────────┘
```

### 2.1 模块职责详解

| 模块 | 文件 | 职责 |
|------|------|------|
| **主入口** | `lspsrv_main.c` | 程序入口点，初始化服务器，启动主事件循环。负责 stdio 消息帧的读取（HTTP 风格 `Content-Length` 头）和写入，以及信号处理。 |
| **JSON 编解码** | `lspsrv_json.c` | 轻量级 JSON-RPC 2.0 解析器与序列化器。支持 `JsonRpcMessage` 的解析（`jrpc_parse`）和序列化（`jrpc_serialize`），以及 JSON 值的创建、查询、修改。 |
| **协议处理** | `lspsrv_proto.c` | **核心路由模块**。包含 `lsp_init`（初始化）、`lsp_handle_message`（消息分发入口）、`dispatch_request`（请求路由）以及所有 LSP 方法的响应构建函数。 |
| **文档管理** | `lspsrv_doc.c` | 管理打开的文档生命周期（didOpen / didChange / didClose）。每份文档维护文本内容、行偏移缓存、Token 流、AST、符号表、变量列表、诊断信息、导入模块列表等。最大支持 64 个并发文档。 |
| **词法分析** | `lspsrv_lexer.c` | 独立的增量词法分析器，将 LXCLUA 源代码分词为 `LspToken` 流。支持 LXCLUA 全部关键字（含 abstract、class、enum、match 等）、运算符、字面量、注释。 |
| **关键字数据库** | `lspsrv_kwdb.c` | 维护 LXCLUA 关键字、内置函数、标准库函数、代码片段的完整数据库，每项包含名称、类型、详细描述、文档字符串和插入片段。 |
| **自动补全** | `lspsrv_complete.c` | 上下文感知的代码补全。分析光标位置上下文（表字段、方法调用、全局、局部、注释、字符串），提供关键字、内置函数、标准库、局部变量、表字段的补全建议。 |
| **悬停与跳转** | `lspsrv_hover.c` | 提供悬停信息（Hover）和定义跳转（Go-to-Definition）。悬停显示局部变量类型、关键字文档、标准库函数说明；跳转支持局部变量定义和 import/require 模块。 |
| **功能特性** | `lspsrv_features.c` | 包含诊断（语法错误、不平衡块、未使用变量、尾部空白、未定义变量）、代码格式化、引用查找、重命名、文档符号、折叠范围、语义标记、代码操作等功能的实现。 |
| **工具函数** | `lspsrv_util.c` | 通用工具函数：内存管理（`lsp_alloc`/`lsp_realloc`/`lsp_free`）、字符串操作（`lsp_strdup`/`lsp_str_append`/`lsp_fmt`）、坐标转换（offset ↔ line/col）、词法辅助（`lsp_get_word_at`、`lsp_is_ident_char`）。 |

### 2.2 数据流

```
编辑器                     LSP 服务器
  │                           │
  │  JSON-RPC (stdin)         │
  ├──────────────────────────►│  main_loop()
  │                           │    │
  │                           │    ├─ read_lsp_message()  ← 读取 Content-Length 帧
  │                           │    │
  │                           │    ├─ lsp_handle_message()
  │                           │    │    ├─ jrpc_parse()          ← JSON 解析
  │                           │    │    ├─ dispatch_request()    ← 方法路由
  │                           │    │    │    ├─ lsp_doc_find()   ← 查找文档
  │                           │    │    │    ├─ 功能函数调用      ← 业务逻辑
  │                           │    │    │    └─ build_xxx()      ← 响应构建
  │                           │    │    └─ jrpc_serialize()      ← JSON 序列化
  │                           │    │
  │                           │    └─ write_lsp_message()  ← 输出 Content-Length 帧
  │  JSON-RPC (stdout)        │
  │◄──────────────────────────┤
```

---

## 3. 通信协议

### 3.1 传输层

LSP 服务器通过 **标准输入/输出（stdio）** 与客户端通信，使用 **HTTP 风格的帧格式**：

```
Content-Length: <N>\r\n
\r\n
<JSON-RPC 消息体>
```

- **读取**：`read_lsp_message()` 逐字节读取头部直到 `\r\n\r\n`，解析 `Content-Length`，然后读取指定长度的消息体。
- **写入**：`jrpc_serialize()` 自动在序列化结果前添加 `Content-Length` 头，`write_lsp_message()` 直接写入 stdout。
- **日志**：所有调试信息通过 `stderr` 输出，不干扰 LSP 通信通道。
- **跨平台**：Windows 上使用 `ReadFile`/`WriteFile` API 并设置二进制模式；Unix 上使用 `read`/`write` 系统调用。

### 3.2 JSON-RPC 2.0

服务器严格遵循 JSON-RPC 2.0 规范：

- **请求（Request）**：包含 `jsonrpc`、`method`、`params`（可选）、`id`（字符串或数字）
- **通知（Notification）**：与请求相同但不含 `id` 字段，服务器不回复
- **响应（Response）**：包含 `jsonrpc`、`id`、`result`（成功）或 `error`（失败）
- **错误码**：支持标准 JSON-RPC 错误码（`-32700` 解析错误、`-32600` 无效请求、`-32601` 方法未找到、`-32602` 无效参数、`-32603` 内部错误）和 LSP 自定义错误码（`-32002` 服务器未初始化）

### 3.3 消息处理流程

```
lsp_handle_message(data, len, &response)
    │
    ├─ jrpc_parse(data, len) → JsonRpcMessage
    │     ├─ 解析失败 → 返回 JRPC_PARSE_ERROR
    │     └─ 解析成功 → 继续
    │
    ├─ jrpc_is_response(msg)? → 忽略（不跟踪待处理请求）
    │
    ├─ jrpc_is_notification(msg)?
    │     ├─ 是 → dispatch_request(srv, method, NULL, params)  → 不返回响应
    │     └─ 否 → dispatch_request(srv, method, id, params)    → 返回响应
    │
    └─ 有响应 → jrpc_serialize(resp) → 写入 stdout
```

---

## 4. 支持的功能

### 4.1 生命周期管理

| 方法 | 类型 | 说明 |
|------|------|------|
| `initialize` | 请求 | 初始化握手，服务器返回能力集（capabilities） |
| `initialized` | 通知 | 客户端确认初始化完成 |
| `shutdown` | 请求 | 客户端请求关闭，服务器不再处理新请求 |
| `exit` | 通知 | 客户端通知服务器退出进程 |

### 4.2 文档同步

| 方法 | 类型 | 说明 |
|------|------|------|
| `textDocument/didOpen` | 通知 | 文档打开，传入完整文本和版本号 |
| `textDocument/didChange` | 通知 | 文档内容变更（全量替换，`TextDocumentSyncKind.Full = 1`） |
| `textDocument/didClose` | 通知 | 文档关闭，释放相关资源 |
| `textDocument/didSave` | 通知 | 文档保存，触发重新解析和诊断 |

- 采用**全量同步**模式，每次变更传输完整文档内容。
- 文档打开时自动进行词法分析（Token 流）、符号提取和诊断分析。
- 最多同时管理 **64 个** 打开文档。

### 4.3 自动补全（`textDocument/completion`）

**请求参数**：`textDocument`（URI）、`position`（line, character）

**上下文分析**（`analyze_context`）支持 6 种上下文：
- `global`：全局上下文，提供关键字、内置函数、标准库、局部变量
- `table_field`：表字段访问（`.`），搜索 `table.field` 模式
- `method_call`：方法调用（`:`），提供方法补全
- `local`：`local` 关键字后，提供类型提示
- `comment`：注释中，无补全
- `string`：字符串中，无补全

**补全来源**：
1. **关键字**：LXCLUA 全部关键字（含扩展关键字），`COMPLETION_KEYWORD` 类型
2. **内置函数**：`print`、`type`、`tostring`、`require` 等，`COMPLETION_FUNCTION` 类型
3. **标准库函数**：`string.format`、`table.insert`、`math.abs` 等
4. **局部变量**：从文档符号表中提取
5. **表字段**：文本扫描 `table.field` 和表初始化器 `{field = value}` 模式
6. **代码片段**：`COMPLETION_SNIPPET` 类型，使用 `INSERT_TEXT_SNIPPET` 格式

**排序优先级**（`sort_text_priority`）：
- 关键字：70
- 函数：60
- 表字段：50
- 代码片段：30
- 前缀匹配额外 +100，大小写完全匹配再 +50

### 4.4 悬停信息（`textDocument/hover`）

**响应格式**：Markdown 内容

**信息层次**：
1. 局部变量：显示类型注解（`local name: Type -- kind`）和定义位置
2. 关键字文档：从关键字数据库查找，显示 Markdown 格式的文档
3. 标准库/模块方法：处理 `string.xxx`、`table.xxx` 等点分隔名称

### 4.5 定义跳转（`textDocument/definition`）

**查找顺序**：
1. 局部变量定义位置（符号表）
2. import/require 模块引用位置

**响应格式**：LSP Location（URI + Range）

### 4.6 引用查找（`textDocument/references`）

搜索整个文档中所有同名标识符的出现位置，返回位置列表。

### 4.7 重命名（`textDocument/rename`）

**流程**：
1. 获取光标处的单词
2. 扫描文档中所有同名 Token
3. 构建 `WorkspaceEdit`，包含所有替换位置
4. 返回 `changes` 映射（URI → TextEdit 数组）

**前置检查**：`textDocument/prepareRename` 验证光标位置是否可重命名。

### 4.8 文档符号（`textDocument/documentSymbol`）

提取文档结构符号，每个符号包含：
- `name`：符号名称
- `kind`：LSP SymbolKind（1-26）
- `range`：符号完整范围
- `selectionRange`：符号名称范围
- `detail`：类型/种类描述

### 4.9 诊断（`textDocument/diagnostic`）

**实时分析**（`lsp_diagnostic`），在文档打开和变更时自动触发。

| 诊断类型 | 严重级别 | 说明 |
|----------|----------|------|
| 不平衡块检测 | Error | 多余的 `end` 或缺少匹配的 `end` |
| 未使用变量 | Warning | 已定义但从未被引用的局部变量 |
| 尾部空白 | Info | 行尾有多余空格或制表符 |
| 未闭合字符串 | Error | 字符串字面量缺少闭合引号或 `]]` |
| 多余逗号 | Warning | 连续两个逗号 |
| 末尾多余逗号 | Info | 表构造器或参数列表末尾的逗号 |
| 未定义变量 | Warning | 引用了未在符号表、关键字库或导入列表中出现的标识符 |

### 4.10 代码格式化（`textDocument/formatting`）

支持 `tabSize` 和 `insertSpaces` 配置选项。返回整个文档的格式化文本编辑。

### 4.11 代码操作（`textDocument/codeAction`）

为每个诊断问题生成对应的快速修复（QuickFix）：

| 诊断 | 修复操作 |
|------|----------|
| 行尾多余空白 | 去除行尾空白字符 |
| 多余的逗号 | 移除多余逗号 |
| 末尾多余逗号 | 移除末尾逗号 |
| 未闭合的长字符串 | 补全 `]]` |
| 未闭合的字符串 | 补全引号 `"` |
| Unclosed block | 添加缺失的 `end` |
| Unexpected 'end' | 移除多余的 `end` |

### 4.12 语义标记（`textDocument/semanticTokens/full`）

为每个 Token 分配标准 LSP 语义类型和修饰符。

**语义类型映射**：
- 变量定义 → `variable`(8)
- 函数定义 → `function`(12)
- 方法定义 → `method`(13)
- 结构体 → `struct`(5)
- 枚举 → `enum`(3)
- 命名空间 → `namespace`(0)
- 类 → `class`(2)
- 接口 → `interface`(4)
- 类型关键字 → `type`(1)
- 控制流关键字 → `keyword`(15)
- 字符串 → `string`(18)
- 注释 → `comment`(17)
- 数字 → `number`(19)
- 运算符 → `operator`(21)

**增量更新**：支持 `semanticTokens/full/delta`（Delta 增量模式）和 `semanticTokens/range`（范围请求）。

### 4.13 折叠范围（`textDocument/foldingRange`）

基于块结构（`function`/`if`/`while`/`do` 等）计算代码折叠范围。

### 4.14 签名帮助（`textDocument/signatureHelp`）

在函数调用括号内提供参数签名提示。

### 4.15 文档高亮（`textDocument/documentHighlight`）

高亮显示文档中与当前标识符相同的所有引用。

### 4.16 其他高级功能

| 方法 | 说明 |
|------|------|
| `textDocument/typeDefinition` | 类型定义跳转 |
| `textDocument/implementation` | 接口/抽象方法实现查找 |
| `textDocument/declaration` | 声明位置跳转 |
| `textDocument/codeLens` | 代码透镜（函数引用计数等） |
| `textDocument/documentLink` | 文档内链接检测 |
| `textDocument/inlayHint` | 内联类型提示 |
| `textDocument/linkedEditingRange` | 同步编辑范围（如 HTML 标签对） |
| `textDocument/selectionRange` | 智能选择范围扩展 |
| `textDocument/onTypeFormatting` | 输入时自动格式化 |
| `textDocument/rangeFormatting` | 范围格式化 |
| `textDocument/colorPresentation` | 颜色值呈现 |
| `textDocument/moniker` | 符号唯一标识符 |
| `textDocument/prepareCallHierarchy` | 调用层次结构准备 |
| `callHierarchy/incomingCalls` | 传入调用 |
| `callHierarchy/outgoingCalls` | 传出调用 |
| `textDocument/prepareTypeHierarchy` | 类型层次结构准备 |
| `typeHierarchy/supertypes` | 父类型 |
| `typeHierarchy/subtypes` | 子类型 |
| `workspace/symbol` | 工作区符号搜索 |
| `workspace/didChangeConfiguration` | 配置变更通知 |

---

## 5. 初始化流程

### 5.1 完整时序

```
客户端                          LSP 服务器
  │                                │
  │  initialize (request)          │
  ├───────────────────────────────►│
  │                                │  lsp_init()
  │                                │  设置所有能力位为 1
  │                                │  构建 capabilities 响应
  │  ←  capabilities response      │
  │◄───────────────────────────────┤
  │                                │
  │  initialized (notification)    │
  ├───────────────────────────────►│
  │                                │  (无操作)
  │                                │
  │  textDocument/didOpen          │
  ├───────────────────────────────►│
  │                                │  lsp_doc_open()
  │                                │  创建 LspDocument
  │                                │  lsp_lex() → 词法分析
  │                                │  lsp_doc_parse() → 符号提取
  │                                │  lsp_diagnostic() → 诊断
  │                                │
  │  ... 正常消息循环 ...           │
  │                                │
  │  shutdown (request)            │
  ├───────────────────────────────►│
  │                                │  srv->shutdown = 1
  │  ←  null response              │
  │◄───────────────────────────────┤
  │                                │
  │  exit (notification)           │
  ├───────────────────────────────►│
  │                                │  srv->exit_requested = 1
  │                                │  main_loop() 退出
  │                                │  lsp_srv_free() 清理资源
```

### 5.2 能力声明

初始化时服务器声明以下能力（全部启用）：

- `textDocumentSync`：`openClose` + `change: Full` + `save`
- `completionProvider`
- `hoverProvider`
- `definitionProvider`、`typeDefinitionProvider`、`implementationProvider`、`declarationProvider`
- `referencesProvider`
- `documentHighlightProvider`
- `documentSymbolProvider`
- `signatureHelpProvider`
- `renameProvider`、`prepareRenameProvider`
- `documentFormattingProvider`、`documentRangeFormattingProvider`、`documentOnTypeFormattingProvider`
- `foldingRangeProvider`
- `semanticTokensProvider`（full + range + delta）
- `codeActionProvider`
- `diagnosticProvider`
- `workspaceSymbolProvider`
- `selectionRangeProvider`
- `linkedEditingRangeProvider`
- `codeLensProvider`
- `documentLinkProvider`
- `inlayHintProvider`
- `callHierarchyProvider`
- `typeHierarchyProvider`
- `colorProvider`
- `monikerProvider`

### 5.3 服务器信息

- **名称**：`lxclua-lsp`
- **版本**：`1.0.0`

---

## 6. 关键数据结构

### 6.1 LspServer（服务器状态）

```c
typedef struct LspServer {
    int initialized;        // 是否已完成 initialize 握手
    int shutdown;           // 是否已收到 shutdown
    int exit_requested;     // 是否已收到 exit
    LspDocument *docs[64];  // 文档存储（最多 64 个）
    int ndocs;              // 当前文档数
    struct { ... } capabilities;  // 服务器能力位掩码
    struct { ... } client_caps;   // 客户端能力位掩码
    int next_request_id;    // 下一个请求 ID
    int prev_semantic_version;    // 上一次语义标记的文档版本
    char *prev_semantic_result_id; // 上一次语义标记的 resultId
} LspServer;
```

### 6.2 LspDocument（文档）

```c
typedef struct LspDocument {
    char *uri;              // 文档 URI
    char *text;             // 文档文本内容
    size_t text_len;        // 文本长度
    int version;            // 文档版本号
    int open;               // 是否打开
    int *line_offsets;      // 行偏移缓存（快速 offset↔line/col 转换）
    int nlines;             // 总行数
    LspToken *tokens;       // 词法分析 Token 流
    int ntokens;            // Token 数量
    AstNode *ast;           // 抽象语法树
    LspSymbol **symbols;    // 文档符号表
    int nsymbols;           // 符号数量
    LspVarInfo *vars;       // 局部变量信息
    int nvars;              // 变量数量
    int var_cap;            // 变量数组容量
    LspDiagnostic *diagnostics; // 诊断信息
    int ndiags;             // 诊断数量
    char **defined_globals; // 定义的全局变量
    char **imports;         // 导入的模块列表
} LspDocument;
```

### 6.3 核心类型枚举

**LSP 协议枚举**：
- `CompletionItemKind`（1-25）：Text、Method、Function、Constructor、Field、Variable、Class、Interface、Module、Property、Unit、Value、Enum、Keyword、Snippet、Color、File、Reference、Folder、EnumMember、Constant、Struct、Event、Operator、TypeParameter
- `SymbolKind`（1-26）：File、Module、Namespace、Package、Class、Method、Property、Field、Constructor、Enum、Interface、Function、Variable、Constant、String、Number、Boolean、Array、Object、Key、Null、EnumMember、Struct、Event、Operator、TypeParameter
- `DiagnosticSeverity`：Error(1)、Warning(2)、Info(3)、Hint(4)

**LXCLUA Token 类型**（`LspTokenType`）：
- 单字符 Token 映射到 ASCII 值（`< 256`）
- 多字符 Token 从 257 开始，覆盖所有 LXCLUA 关键字、运算符和字面量类型

---

## 7. 使用方式

### 7.1 VS Code 配置示例

```json
{
  "languages": [{
    "id": "lxclua",
    "extensions": [".lua", ".lxclua"],
    "configuration": "./language-configuration.json"
  }],
  "servers": {
    "lxclua-lsp": {
      "command": "lxclua-lsp",
      "args": []
    }
  }
}
```

### 7.2 命令行启动

```bash
# 直接启动（编辑器通过 stdio 连接）
./lxclua-lsp

# Windows
lxclua-lsp.exe
```

### 7.3 日志输出

所有日志信息通过 **stderr** 输出，可以在 VS Code 的输出面板中查看（选择 "lxclua-lsp" 通道）。

---

## 8. 文件结构

```
src/lspsrv/
├── lspsrv.h              # 主头文件：所有类型定义、枚举、函数声明
├── lspsrv_main.c         # 主入口：程序入口、信号处理、消息循环、stdio I/O
├── lspsrv_json.c         # JSON 编解码：JSON-RPC 2.0 解析器和序列化器
├── lspsrv_proto.c        # 协议处理：请求路由、响应构建、初始化
├── lspsrv_doc.c          # 文档管理：文档生命周期、符号表、解析
├── lspsrv_lexer.c        # 词法分析：增量词法分析器
├── lspsrv_kwdb.c         # 关键字数据库：关键字、内置函数、标准库
├── lspsrv_complete.c     # 自动补全：上下文分析、补全建议
├── lspsrv_hover.c        # 悬停与跳转：Hover、Go-to-Definition
├── lspsrv_features.c     # 功能特性：诊断、格式化、代码操作等
└── lspsrv_util.c         # 工具函数：内存管理、字符串、坐标转换
```