/*
** LXCLUA LSP - JSON parser and serializer
** Lightweight JSON-RPC 2.0 compliant JSON handling.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "lspsrv.h"

/* Forward declarations for internal use */
static JsonValue *parse_value(const char *src, int len, int *pos);
static char *json_stringify_recursive(JsonValue *v, int indent, int level);

/*
 * @brief 跳过空白字符
 * @param src 源数据
 * @param len 长度
 * @param pos 当前位置（将被更新）
 */
static void skip_whitespace(const char *src, int len, int *pos) {
    while (*pos < len && (src[*pos] == ' ' || src[*pos] == '\t' || 
           src[*pos] == '\n' || src[*pos] == '\r')) {
        (*pos)++;
    }
}

/*
 * @brief 解析JSON字符串
 * @param src 源数据
 * @param len 长度
 * @param pos 当前位置（指向"后的第一个字符）
 * @return 解析的字符串值
 */
static char *parse_string(const char *src, int len, int *pos) {
    int cap = 64, idx = 0;
    char *buf = (char *)lsp_alloc(cap);
    while (*pos < len) {
        char c = src[*pos];
        if (c == '"') { (*pos)++; buf[idx] = 0; return buf; }
        if (c == '\\') {
            (*pos)++;
            if (*pos >= len) break;
            char esc = src[*pos];
            switch (esc) {
                case '"':  buf[idx++] = '"';  break;
                case '\\': buf[idx++] = '\\'; break;
                case '/':  buf[idx++] = '/';  break;
                case 'b':  buf[idx++] = '\b'; break;
                case 'f':  buf[idx++] = '\f'; break;
                case 'n':  buf[idx++] = '\n'; break;
                case 'r':  buf[idx++] = '\r'; break;
                case 't':  buf[idx++] = '\t'; break;
                case 'u': {
                    /* \uXXXX unicode escape - convert to UTF-8 */
                    unsigned int cp = 0;
                    for (int i = 0; i < 4 && *pos+1 < len; i++) {
                        (*pos)++;
                        char h = src[*pos];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                    }
                    /* Encode as UTF-8 */
                    if (cp < 0x80) {
                        buf[idx++] = (char)cp;
                    } else if (cp < 0x800) {
                        buf[idx++] = (char)(0xC0 | (cp >> 6));
                        buf[idx++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        buf[idx++] = (char)(0xE0 | (cp >> 12));
                        buf[idx++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        buf[idx++] = (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: buf[idx++] = esc; break;
            }
        } else {
            buf[idx++] = c;
        }
        if (idx + 4 >= cap) { cap *= 2; buf = lsp_realloc(buf, cap); }
        (*pos)++;
    }
    buf[idx] = 0;
    return buf;
}

/*
 * @brief 解析JSON数字
 * @param src 源数据
 * @param len 长度
 * @param pos 当前位置
 * @return 解析的数值
 */
static double parse_number(const char *src, int len, int *pos) {
    int start = *pos;
    while (*pos < len && (src[*pos] == '-' || src[*pos] == '+' || src[*pos] == '.' ||
           src[*pos] == 'e' || src[*pos] == 'E' ||
           (src[*pos] >= '0' && src[*pos] <= '9'))) {
        (*pos)++;
    }
    char *endptr = NULL;
    /* Copy to a temp buffer for strtod */
    int numlen = *pos - start;
    char *buf = (char *)lsp_alloc(numlen + 1);
    memcpy(buf, src + start, numlen);
    buf[numlen] = 0;
    double val = strtod(buf, &endptr);
    lsp_free(buf);
    return val;
}

/*
 * @brief 解析JSON数组
 * @param src 源数据
 * @param len 长度
 * @param pos 当前位置
 * @return 数组JSON值
 */
static JsonValue *parse_array(const char *src, int len, int *pos) {
    JsonValue *arr = json_new_array();
    skip_whitespace(src, len, pos);
    if (src[*pos] == ']') { (*pos)++; return arr; }
    while (1) {
        skip_whitespace(src, len, pos);
        JsonValue *item = parse_value(src, len, pos);
        if (item) json_array_add(arr, item);
        skip_whitespace(src, len, pos);
        if (*pos >= len) break;
        if (src[*pos] == ',') { (*pos)++; continue; }
        if (src[*pos] == ']') { (*pos)++; break; }
        break;
    }
    return arr;
}

/*
 * @brief 解析JSON对象
 * @param src 源数据
 * @param len 长度
 * @param pos 当前位置
 * @return 对象JSON值
 */
static JsonValue *parse_object(const char *src, int len, int *pos) {
    JsonValue *obj = json_new_object();
    skip_whitespace(src, len, pos);
    if (src[*pos] == '}') { (*pos)++; return obj; }
    while (1) {
        skip_whitespace(src, len, pos);
        if (*pos >= len || src[*pos] != '"') break;
        (*pos)++; /* skip opening " */
        char *key = parse_string(src, len, pos);
        skip_whitespace(src, len, pos);
        if (*pos < len && src[*pos] == ':') (*pos)++;
        skip_whitespace(src, len, pos);
        JsonValue *val = parse_value(src, len, pos);
        if (key && val) {
            /* Inline add member to avoid re-parse */
            obj->as.obj.count++;
            obj->as.obj.members = lsp_realloc(obj->as.obj.members, obj->as.obj.count * sizeof(JsonMember));
            obj->as.obj.members[obj->as.obj.count - 1].key = key;
            obj->as.obj.members[obj->as.obj.count - 1].value = val;
        } else {
            lsp_free(key);
            if (val) json_free(val);
        }
        skip_whitespace(src, len, pos);
        if (*pos >= len) break;
        if (src[*pos] == ',') { (*pos)++; continue; }
        if (src[*pos] == '}') { (*pos)++; break; }
        break;
    }
    return obj;
}

/*
 * @brief 解析JSON值（递归解析入口）
 * @param src 源数据
 * @param len 长度
 * @param pos 当前位置
 * @return 解析的JSON值，NULL表示解析失败
 */
static JsonValue *parse_value(const char *src, int len, int *pos) {
    skip_whitespace(src, len, pos);
    if (*pos >= len) return NULL;
    char c = src[*pos];
    if (c == '"') {
        (*pos)++;
        char *s = parse_string(src, len, pos);
        return json_new_string(s);
    } else if (c == '{') {
        (*pos)++;
        return parse_object(src, len, pos);
    } else if (c == '[') {
        (*pos)++;
        return parse_array(src, len, pos);
    } else if (c == 't' && len - *pos >= 4 && 
               src[*pos+1]=='r' && src[*pos+2]=='u' && src[*pos+3]=='e') {
        *pos += 4;
        return json_new_bool(1);
    } else if (c == 'f' && len - *pos >= 5 &&
               src[*pos+1]=='a' && src[*pos+2]=='l' && src[*pos+3]=='s' && src[*pos+4]=='e') {
        *pos += 5;
        return json_new_bool(0);
    } else if (c == 'n' && len - *pos >= 4 &&
               src[*pos+1]=='u' && src[*pos+2]=='l' && src[*pos+3]=='l') {
        *pos += 4;
        return json_new_null();
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        return json_new_number(parse_number(src, len, pos));
    }
    return NULL;
}

/*
 * @brief 解析完整JSON数据
 * @param src JSON源字符串
 * @param len 长度
 * @return 解析的根JSON值，NULL表示解析失败
 */
JsonValue *json_parse(const char *src, int len) {
    int pos = 0;
    return parse_value(src, len, &pos);
}

/* === JSON Builder === */

/*
 * @brief 创建JSON null值
 * @return JSON值
 */
JsonValue *json_new_null(void) {
    JsonValue *v = (JsonValue *)lsp_alloc(sizeof(JsonValue));
    v->type = JSON_NULL;
    return v;
}

/*
 * @brief 创建JSON布尔值
 * @param val 布尔值（0或1）
 * @return JSON值
 */
JsonValue *json_new_bool(int val) {
    JsonValue *v = (JsonValue *)lsp_alloc(sizeof(JsonValue));
    v->type = JSON_BOOL;
    v->as.bool_val = val ? 1 : 0;
    return v;
}

/*
 * @brief 创建JSON数字值
 * @param val 数值
 * @return JSON值
 */
JsonValue *json_new_number(double val) {
    JsonValue *v = (JsonValue *)lsp_alloc(sizeof(JsonValue));
    v->type = JSON_NUMBER;
    v->as.num_val = val;
    return v;
}

/*
 * @brief 创建JSON字符串值
 * @param s 源字符串（内部会复制）
 * @return JSON值
 */
JsonValue *json_new_string(const char *s) {
    JsonValue *v = (JsonValue *)lsp_alloc(sizeof(JsonValue));
    v->type = JSON_STRING;
    v->as.str_val = lsp_strdup(s);
    return v;
}

/*
 * @brief 创建空JSON数组
 * @return JSON值
 */
JsonValue *json_new_array(void) {
    JsonValue *v = (JsonValue *)lsp_alloc(sizeof(JsonValue));
    v->type = JSON_ARRAY;
    v->as.arr.items = NULL;
    v->as.arr.count = 0;
    return v;
}

/*
 * @brief 创建空JSON对象
 * @return JSON值
 */
JsonValue *json_new_object(void) {
    JsonValue *v = (JsonValue *)lsp_alloc(sizeof(JsonValue));
    v->type = JSON_OBJECT;
    v->as.obj.members = NULL;
    v->as.obj.count = 0;
    return v;
}

/*
 * @brief 向JSON数组添加元素
 * @param arr 数组值
 * @param item 要添加的元素值（数组接管所有权）
 */
void json_array_add(JsonValue *arr, JsonValue *item) {
    if (!arr || arr->type != JSON_ARRAY) return;
    arr->as.arr.count++;
    arr->as.arr.items = lsp_realloc(arr->as.arr.items, arr->as.arr.count * sizeof(JsonValue *));
    arr->as.arr.items[arr->as.arr.count - 1] = item;
}

/*
 * @brief 设置对象属性
 * @param obj 对象值
 * @param key 属性名
 * @param val 属性值（对象接管所有权）
 */
void json_object_set(JsonValue *obj, const char *key, JsonValue *val) {
    if (!obj || obj->type != JSON_OBJECT || !key) return;
    /* Check for existing key */
    for (int i = 0; i < obj->as.obj.count; i++) {
        if (strcmp(obj->as.obj.members[i].key, key) == 0) {
            json_free(obj->as.obj.members[i].value);
            obj->as.obj.members[i].value = val;
            return;
        }
    }
    obj->as.obj.count++;
    obj->as.obj.members = lsp_realloc(obj->as.obj.members, obj->as.obj.count * sizeof(JsonMember));
    obj->as.obj.members[obj->as.obj.count - 1].key = lsp_strdup(key);
    obj->as.obj.members[obj->as.obj.count - 1].value = val;
}

/*
 * @brief 从对象中获取属性值
 * @param obj 对象值
 * @param key 属性名
 * @return 属性值指针（不可释放），NULL表示未找到
 */
JsonValue *json_object_get(JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    for (int i = 0; i < obj->as.obj.count; i++) {
        if (strcmp(obj->as.obj.members[i].key, key) == 0)
            return obj->as.obj.members[i].value;
    }
    return NULL;
}

/*
 * @brief 从对象中获取整数属性值
 * @param obj 对象值
 * @param key 属性名
 * @param def 未找到时的默认值
 * @return 整数值
 */
int json_object_get_int(JsonValue *obj, const char *key, int def) {
    JsonValue *v = json_object_get(obj, key);
    if (!v || v->type != JSON_NUMBER) return def;
    return (int)v->as.num_val;
}

/*
 * @brief 从对象中获取数字属性值
 * @param obj 对象值
 * @param key 属性名
 * @param def 默认值
 * @return 数值
 */
double json_object_get_number(JsonValue *obj, const char *key, double def) {
    JsonValue *v = json_object_get(obj, key);
    if (!v || v->type != JSON_NUMBER) return def;
    return v->as.num_val;
}

/*
 * @brief 从对象中获取字符串属性值
 * @param obj 对象值
 * @param key 属性名
 * @param def 默认值
 * @return 字符串指针（不可释放）
 */
const char *json_object_get_string(JsonValue *obj, const char *key, const char *def) {
    JsonValue *v = json_object_get(obj, key);
    if (!v || v->type != JSON_STRING) return def;
    return v->as.str_val;
}

/*
 * @brief 从对象中获取布尔属性值
 * @param obj 对象值
 * @param key 属性名
 * @param def 默认值
 * @return 布尔值（0或1）
 */
int json_object_get_bool(JsonValue *obj, const char *key, int def) {
    JsonValue *v = json_object_get(obj, key);
    if (!v || v->type != JSON_BOOL) return def;
    return v->as.bool_val;
}

/*
 * @brief 从数组中获取指定索引的元素
 * @param arr 数组值
 * @param idx 索引（0起始）
 * @return 元素值指针（不可释放），NULL表示越界
 */
JsonValue *json_array_get(JsonValue *arr, int idx) {
    if (!arr || arr->type != JSON_ARRAY || idx < 0 || idx >= arr->as.arr.count) return NULL;
    return arr->as.arr.items[idx];
}

/*
 * @brief 获取数组长度
 * @param arr 数组值
 * @return 数组元素数
 */
int json_array_len(JsonValue *arr) {
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->as.arr.count;
}

/* === JSON Serializer === */

/*
 * @brief 转义JSON字符串中的特殊字符
 * @param s 输入字符串
 * @return 转义后的新字符串
 */
static char *json_escape(const char *s) {
    if (!s) return lsp_strdup("");
    /* Estimate size */
    size_t len = strlen(s);
    size_t cap = len * 2 + 4;
    char *buf = (char *)lsp_alloc(cap);
    size_t idx = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') { buf[idx++]='\\'; buf[idx++]='"'; }
        else if (c == '\\') { buf[idx++]='\\'; buf[idx++]='\\'; }
        else if (c == '\n') { buf[idx++]='\\'; buf[idx++]='n'; }
        else if (c == '\r') { buf[idx++]='\\'; buf[idx++]='r'; }
        else if (c == '\t') { buf[idx++]='\\'; buf[idx++]='t'; }
        else if (c == '\b') { buf[idx++]='\\'; buf[idx++]='b'; }
        else if (c == '\f') { buf[idx++]='\\'; buf[idx++]='f'; }
        else if (c < 0x20) {
            /* Control character - use \uXXXX */
            buf[idx++]='\\'; buf[idx++]='u';
            snprintf(buf + idx, 5, "%04x", c);
            idx += 4;
        } else {
            buf[idx++] = s[i];
        }
        if (idx + 10 > cap) { cap *= 2; buf = lsp_realloc(buf, cap); }
    }
    buf[idx] = 0;
    return buf;
}

/*
 * @brief 将JSON序列化为字符串（内部递归实现）
 * @param v JSON值
 * @param indent 缩进空格数（0表示紧凑）
 * @param level 当前缩进层级
 * @return 序列化后的字符串
 */
static char *json_stringify_recursive(JsonValue *v, int indent, int level) {
    if (!v) return lsp_strdup("null");
    char *result = NULL;
    char buf[128];
    
    switch (v->type) {
        case JSON_NULL:
            return lsp_strdup("null");
        case JSON_BOOL:
            return lsp_strdup(v->as.bool_val ? "true" : "false");
        case JSON_NUMBER: {
            if (floor(v->as.num_val) == v->as.num_val) {
                snprintf(buf, sizeof(buf), "%.0f", v->as.num_val);
            } else {
                snprintf(buf, sizeof(buf), "%g", v->as.num_val);
            }
            return lsp_strdup(buf);
        }
        case JSON_STRING: {
            char *esc = json_escape(v->as.str_val);
            char *s = (char *)lsp_alloc(strlen(esc) + 3);
            snprintf(s, strlen(esc) + 3, "\"%s\"", esc);
            lsp_free(esc);
            return s;
        }
        case JSON_ARRAY: {
            result = lsp_strdup("[");
            for (int i = 0; i < v->as.arr.count; i++) {
                if (i > 0) result = lsp_str_append(result, ",");
                char *item = json_stringify_recursive(v->as.arr.items[i], indent, level + 1);
                result = lsp_str_append(result, item);
                lsp_free(item);
            }
            result = lsp_str_append(result, "]");
            return result;
        }
        case JSON_OBJECT: {
            result = lsp_strdup("{");
            for (int i = 0; i < v->as.obj.count; i++) {
                if (i > 0) result = lsp_str_append(result, ",");
                char *esc_key = json_escape(v->as.obj.members[i].key);
                result = lsp_str_append(result, "\"");
                result = lsp_str_append(result, esc_key);
                result = lsp_str_append(result, "\":");
                lsp_free(esc_key);
                char *val_str = json_stringify_recursive(v->as.obj.members[i].value, indent, level + 1);
                result = lsp_str_append(result, val_str);
                lsp_free(val_str);
            }
            result = lsp_str_append(result, "}");
            return result;
        }
    }
    return lsp_strdup("null");
}

/*
 * @brief 将JSON值序列化为字符串
 * @param v JSON值
 * @return 序列化后的JSON字符串
 */
char *json_stringify(JsonValue *v) {
    return json_stringify_recursive(v, 0, 0);
}

/*
 * @brief 释放JSON值占用的所有内存
 * @param v JSON值
 */
void json_free(JsonValue *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING:
            lsp_free(v->as.str_val);
            break;
        case JSON_ARRAY:
            for (int i = 0; i < v->as.arr.count; i++) json_free(v->as.arr.items[i]);
            lsp_free(v->as.arr.items);
            break;
        case JSON_OBJECT:
            for (int i = 0; i < v->as.obj.count; i++) {
                lsp_free(v->as.obj.members[i].key);
                json_free(v->as.obj.members[i].value);
            }
            lsp_free(v->as.obj.members);
            break;
        default: break;
    }
    lsp_free(v);
}

/*
 * @brief 深度复制JSON值
 * @param dst 目标指针
 * @param src 源值
 */
void json_deep_copy(JsonValue **dst, JsonValue *src) {
    if (!src) { *dst = NULL; return; }
    switch (src->type) {
        case JSON_NULL:    *dst = json_new_null(); return;
        case JSON_BOOL:    *dst = json_new_bool(src->as.bool_val); return;
        case JSON_NUMBER:  *dst = json_new_number(src->as.num_val); return;
        case JSON_STRING:  *dst = json_new_string(src->as.str_val); return;
        case JSON_ARRAY: {
            JsonValue *arr = json_new_array();
            for (int i = 0; i < src->as.arr.count; i++) {
                JsonValue *item = NULL;
                json_deep_copy(&item, src->as.arr.items[i]);
                if (item) json_array_add(arr, item);
            }
            *dst = arr;
            return;
        }
        case JSON_OBJECT: {
            JsonValue *obj = json_new_object();
            for (int i = 0; i < src->as.obj.count; i++) {
                JsonValue *val = NULL;
                json_deep_copy(&val, src->as.obj.members[i].value);
                json_object_set(obj, src->as.obj.members[i].key, val);
            }
            *dst = obj;
            return;
        }
    }
}

/* === JSON-RPC Message Handling === */

/*
 * @brief 解析JSON-RPC 2.0消息
 * @param data 原始二进制数据
 * @param len 数据长度
 * @return 解析的JSON-RPC消息，NULL表示解析失败
 */
JsonRpcMessage *jrpc_parse(const char *data, int len) {
    /* Parse Content-Length header */
    int content_len = -1;
    const char *body = data;
    int body_len = len;
    
    if (len > 16 && strncmp(data, "Content-Length:", 15) == 0) {
        content_len = atoi(data + 15);
        /* Find \r\n\r\n separator */
        const char *sep = strstr(data, "\r\n\r\n");
        if (sep) {
            body = sep + 4;
            body_len = content_len;
        } else {
            sep = strstr(data, "\n\n");
            if (sep) {
                body = sep + 2;
                body_len = content_len;
            }
        }
    }
    
    JsonValue *root = json_parse(body, body_len);
    if (!root || root->type != JSON_OBJECT) {
        if (root) json_free(root);
        return NULL;
    }
    
    JsonRpcMessage *msg = (JsonRpcMessage *)lsp_alloc(sizeof(JsonRpcMessage));
    msg->jsonrpc = lsp_strdup(json_object_get_string(root, "jsonrpc", "2.0"));
    msg->method = lsp_strdup(json_object_get_string(root, "method", NULL));
    JsonValue *params_val = json_object_get(root, "params");
    if (params_val) json_deep_copy(&msg->params, params_val);
    JsonValue *result_val = json_object_get(root, "result");
    if (result_val) json_deep_copy(&msg->result, result_val);
    JsonValue *error_val = json_object_get(root, "error");
    if (error_val) json_deep_copy(&msg->error, error_val);
    JsonValue *id_val = json_object_get(root, "id");
    if (id_val) json_deep_copy(&msg->id, id_val);
    
    json_free(root);
    return msg;
}

/*
 * @brief 序列化JSON-RPC消息为HTTP风格的LSP传输格式
 * @param msg JSON-RPC消息
 * @return 包含Content-Length头部和JSON正文的完整字符串
 */
char *jrpc_serialize(JsonRpcMessage *msg) {
    /* Build the JSON body first */
    JsonValue *root = json_new_object();
    json_object_set(root, "jsonrpc", json_new_string("2.0"));
    if (msg->method) json_object_set(root, "method", json_new_string(msg->method));
    if (msg->params) { JsonValue *p = NULL; json_deep_copy(&p, msg->params); json_object_set(root, "params", p); }
    if (msg->result) { JsonValue *r = NULL; json_deep_copy(&r, msg->result); json_object_set(root, "result", r); }
    if (msg->error) { JsonValue *e = NULL; json_deep_copy(&e, msg->error); json_object_set(root, "error", e); }
    if (msg->id) { JsonValue *i = NULL; json_deep_copy(&i, msg->id); json_object_set(root, "id", i); }
    
    char *json_body = json_stringify(root);
    json_free(root);
    
    /* Format as Content-Length: N\r\n\r\n{json} */
    int body_len = (int)strlen(json_body);
    char header[64];
    snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", body_len);
    char *full = (char *)lsp_alloc(strlen(header) + body_len + 1);
    snprintf(full, strlen(header) + body_len + 1, "%s%s", header, json_body);
    lsp_free(json_body);
    return full;
}

/*
 * @brief 释放JSON-RPC消息的内存
 * @param msg 消息指针
 */
void jrpc_free(JsonRpcMessage *msg) {
    if (!msg) return;
    lsp_free(msg->jsonrpc);
    lsp_free(msg->method);
    if (msg->params) json_free(msg->params);
    if (msg->result) json_free(msg->result);
    if (msg->error) json_free(msg->error);
    if (msg->id) json_free(msg->id);
    lsp_free(msg);
}

/*
 * @brief 创建成功的响应消息
 * @param id 请求ID
 * @param result 响应结果
 * @return JSON-RPC响应消息
 */
JsonRpcMessage *jrpc_new_response(JsonValue *id, JsonValue *result) {
    JsonRpcMessage *msg = (JsonRpcMessage *)lsp_alloc(sizeof(JsonRpcMessage));
    msg->jsonrpc = lsp_strdup("2.0");
    if (id) json_deep_copy(&msg->id, id);
    if (result) json_deep_copy(&msg->result, result);
    return msg;
}

/*
 * @brief 创建错误响应消息
 * @param id 请求ID
 * @param code 错误代码
 * @param message 错误描述
 * @return JSON-RPC错误响应消息
 */
JsonRpcMessage *jrpc_new_error_resp(JsonValue *id, int code, const char *message) {
    JsonRpcMessage *msg = (JsonRpcMessage *)lsp_alloc(sizeof(JsonRpcMessage));
    msg->jsonrpc = lsp_strdup("2.0");
    if (id) json_deep_copy(&msg->id, id);
    JsonValue *err = json_new_object();
    json_object_set(err, "code", json_new_number(code));
    json_object_set(err, "message", json_new_string(message));
    msg->error = err;
    return msg;
}

/*
 * @brief 创建通知消息
 * @param method 方法名
 * @param params 参数
 * @return JSON-RPC通知消息
 */
JsonRpcMessage *jrpc_new_notification(const char *method, JsonValue *params) {
    JsonRpcMessage *msg = (JsonRpcMessage *)lsp_alloc(sizeof(JsonRpcMessage));
    msg->jsonrpc = lsp_strdup("2.0");
    msg->method = lsp_strdup(method);
    if (params) json_deep_copy(&msg->params, params);
    return msg;
}

/*
 * @brief 判断消息是否为通知（无id字段）
 * @param msg JSON-RPC消息
 * @return 1是通知，0非通知
 */
int jrpc_is_notification(JsonRpcMessage *msg) {
    return msg && msg->id == NULL && msg->method != NULL;
}

/*
 * @brief 判断消息是否为响应（无method字段）
 * @param msg JSON-RPC消息
 * @return 1是响应，0非响应
 */
int jrpc_is_response(JsonRpcMessage *msg) {
    return msg && msg->method == NULL;
}