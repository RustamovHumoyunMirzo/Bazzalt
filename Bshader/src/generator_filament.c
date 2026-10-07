#include "generator_filament.h"
#include "strbuf.h"
#include "strset.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- output-field mapping (color = ..., roughness = ..., etc) ---- */

typedef struct sc_output_field {
    const char* bshader_name;
    const char* filament_lvalue;
} sc_output_field;

static const sc_output_field SC_OUTPUT_FIELDS[] = {
    {"color", "material.baseColor"},
    {"alpha", "material.baseColor.a"},
    {"roughness", "material.roughness"},
    {"metallic", "material.metallic"},
    {"normal", "material.normal"},
    {"ao", "material.ambientOcclusion"},
    {"emissive", "material.emissive"},
};
static const sc_output_field SC_VERTEX_FIELDS[]={{"position","material.worldPosition.xyz"},{"normal","material.worldNormal"},{"UV","material.uv0"},{"vertexColor","material.color"}};
#define SC_OUTPUT_FIELD_COUNT (sizeof(SC_OUTPUT_FIELDS) / sizeof(SC_OUTPUT_FIELDS[0]))

static const char* sc_lookup_output_field(const char* name) {
    for (size_t i = 0; i < SC_OUTPUT_FIELD_COUNT; i++) {
        if (strcmp(SC_OUTPUT_FIELDS[i].bshader_name, name) == 0) return SC_OUTPUT_FIELDS[i].filament_lvalue;
    }
    return NULL;
}

/* ---- built-in identifiers available inside every material { } block,
 * independent of user-declared properties or locals (mirrors the
 * fragment-stage inputs a real material shader would have in scope) ---- */

static const char* SC_BUILTIN_IDENTIFIERS[] = {
    "UV", "worldPosition", "worldNormal", "vertexColor", "time", "cameraPosition", "viewMatrix", "projectionMatrix", "modelMatrix", "position",
};
#define SC_BUILTIN_IDENTIFIER_COUNT (sizeof(SC_BUILTIN_IDENTIFIERS) / sizeof(SC_BUILTIN_IDENTIFIERS[0]))

static int sc_is_builtin_identifier(const char* name) {
    for (size_t i = 0; i < SC_BUILTIN_IDENTIFIER_COUNT; i++) {
        if (strcmp(SC_BUILTIN_IDENTIFIERS[i], name) == 0) return 1;
    }
    return 0;
}

/* ---- property type -> Filament parameter type text ---- */

static const char* sc_filament_param_type(sc_property_type type) {
    switch (type) {
        case SC_PROP_FLOAT: return "float";
        case SC_PROP_INT: return "int";
        case SC_PROP_BOOL: return "bool";
        case SC_PROP_VEC2: return "float2";
        case SC_PROP_VEC3: return "float3";
        case SC_PROP_VEC4: return "float4";
        case SC_PROP_TEXTURE2D: return "sampler2d";
        case SC_PROP_MAT3: return "mat3";
        case SC_PROP_MAT4: return "mat4";
        default: return "float";
    }
}

/* Filament separates sampler uniforms from the ordinary parameter struct. */
static void sc_emit_property_ref(sc_strbuf* out, const char* name, int sampler) {
    sc_strbuf_appendf(out, sampler ? "materialParams_%s" : "materialParams.%s", name);
}

/* ---- expression codegen ---- */

typedef struct sc_gen_ctx {
    const sc_transform_registry* registry;
    sc_strset* locals;
    sc_strset* properties;
    sc_strset* samplers;
    sc_strset* required_helpers; /* global, dedups helper injection */
    sc_diag_list* diags;
    int had_semantic_error;
    int vertex;
    int unlit;
    int use_uv;
    int use_color;
    int use_normal;
    unsigned int loop_id;
    size_t scope_start;
    int expression_depth;
} sc_gen_ctx;

static void sc_emit_expr(sc_gen_ctx* ctx, sc_expr* expr, sc_strbuf* pre_out, sc_strbuf* out);

static void sc_emit_call(sc_gen_ctx* ctx, sc_expr* expr, sc_strbuf* pre_out, sc_strbuf* out) {
    /* Built-in blur must preserve arbitrary argument expressions, not silently
       replace UV/radius calculations with a guessed identifier/default. */
    const sc_transform_entry* builtin=sc_transform_registry_find(ctx->registry,"blur");
    if(!strcmp(expr->name,"blur")&&builtin&&sc_is_builtin_blur(builtin)){
        size_t count=0;for(sc_expr_list* it=expr->args;it;it=it->next)++count;
        if(ctx->vertex||count<2||count>3){sc_diag_add(ctx->diags,SC_DIAG_ERROR,expr->line,1,"blur requires a fragment-stage texture, UV and optional radius");ctx->had_semantic_error=1;sc_strbuf_append(out,"vec4(0.0)");return;}
        if(!sc_strset_contains(ctx->required_helpers,"sc_blur_sample"))sc_strset_add(ctx->required_helpers,sc_strdup("sc_blur_sample"));
        sc_strbuf_append(out,"sc_blur_sample(");
        for(sc_expr_list* it=expr->args;it;it=it->next){sc_emit_expr(ctx,it->expr,pre_out,out);if(it->next)sc_strbuf_append(out,", ");}
        if(count==2)sc_strbuf_append(out,", 1.0");
        sc_strbuf_append(out,")");return;
    }
    const sc_transform_entry* transform = sc_transform_registry_find(ctx->registry, expr->name);

    if (transform) {
        ShaderTransformNode node;
        node.call_expr = expr;
        node.properties = ctx->properties;
        node.samplers = ctx->samplers;
        node.resolve_scratch[0] = '\0';
        struct ShaderTransformBuilder builder;
        sc_transform_builder_init(&builder);

        transform->fn(&node, &builder, transform->user_data);

        for (size_t i = 0; i < builder.pre_line_count; i++) {
            sc_strbuf_appendf(pre_out, "        %s\n", builder.pre_lines[i]);
        }
        for (size_t i = 0; i < builder.required_helper_count; i++) {
            /* The builder (and the strings it owns) is disposed below,
             * before this shared, longer-lived set is consumed -- so
             * this set must hold its own copies, not raw pointers into
             * builder-owned memory. Freed alongside the set itself at
             * the end of sc_generate_filament_material(). */
            if (!sc_strset_contains(ctx->required_helpers, builder.required_helpers[i])) {
                sc_strset_add(ctx->required_helpers, sc_strdup(builder.required_helpers[i]));
            }
        }

        if (builder.replacement_expr) {
            sc_strbuf_append(out, builder.replacement_expr);
        } else {
            sc_diag_add(ctx->diags, SC_DIAG_ERROR, expr->line, 1,
                        "transform '%s' did not produce a replacement expression",
                        expr->name);
            ctx->had_semantic_error=1;
            sc_strbuf_append(out, "0.0");
        }

        sc_transform_builder_dispose(&builder);
        return;
    }

    /* Not a registered transform: pass through as an ordinary function
     * call. This covers built-in intrinsics like texture(sampler, uv)
     * as well as any target-language function the author calls
     * directly. */
    sc_strbuf_appendf(out, "%s(", expr->name);
    for (sc_expr_list* it = expr->args; it; it = it->next) {
        sc_emit_expr(ctx, it->expr, pre_out, out);
        if (it->next) sc_strbuf_append(out, ", ");
    }
    sc_strbuf_append(out, ")");
}

static void sc_emit_expr(sc_gen_ctx* ctx, sc_expr* expr, sc_strbuf* pre_out, sc_strbuf* out) {
    if(++ctx->expression_depth>128){sc_diag_add(ctx->diags,SC_DIAG_ERROR,expr?expr->line:1,1,"expression complexity limit exceeded");sc_strbuf_append(out,"0.0");--ctx->expression_depth;return;}
    if (!expr) {
        sc_strbuf_append(out, "0.0");
        --ctx->expression_depth;
        return;
    }

    switch (expr->kind) {
        case SC_EXPR_NUMBER: {
            if(expr->name){sc_strbuf_append(out,expr->name);break;}
            char buf[64];
            snprintf(buf, sizeof(buf), "%.9g", expr->number_value);
            sc_strbuf_append(out, buf);
            if(expr->number_is_float&&!strchr(buf,'.')&&!strchr(buf,'e')&&!strchr(buf,'E'))sc_strbuf_append(out,".0");
            break;
        }
        case SC_EXPR_BOOL:
            sc_strbuf_append(out, expr->bool_value ? "true" : "false");
            break;
        case SC_EXPR_IDENTIFIER:
            if (sc_strset_contains(ctx->properties, expr->name)) {
                sc_emit_property_ref(out, expr->name, sc_strset_contains(ctx->samplers,expr->name));
            } else if (sc_strset_contains(ctx->locals, expr->name)) {
                sc_strbuf_append(out, expr->name);
            } else if (sc_is_builtin_identifier(expr->name)) {
                const char* name=expr->name;
                if(!strcmp(name,"UV")){ctx->use_uv=1;sc_strbuf_append(out,ctx->vertex?"material.uv0":"getUV0()");}
                else if(!strcmp(name,"vertexColor")){ctx->use_color=1;sc_strbuf_append(out,ctx->vertex?"material.color":"getColor()");}
                else if(!strcmp(name,"worldPosition"))sc_strbuf_append(out,ctx->vertex?"material.worldPosition.xyz":"getWorldPosition()");
                else if(!strcmp(name,"worldNormal")){ctx->use_normal=1;sc_strbuf_append(out,ctx->vertex?"material.worldNormal":"getWorldGeometricNormalVector()");}
                else if(!strcmp(name,"time"))sc_strbuf_append(out,"getUserTime().x");
                else if(!strcmp(name,"cameraPosition"))sc_strbuf_append(out,"getWorldCameraPosition()");
                else if(!strcmp(name,"viewMatrix"))sc_strbuf_append(out,"getViewFromWorldMatrix()");
                else if(!strcmp(name,"projectionMatrix"))sc_strbuf_append(out,"getClipFromViewMatrix()");
                else if(!strcmp(name,"modelMatrix")){
                    if(!ctx->vertex)sc_diag_add(ctx->diags,SC_DIAG_ERROR,expr->line,1,"modelMatrix is available only in vertex stage");
                    sc_strbuf_append(out,"getWorldFromModelMatrix()");
                }else if(!strcmp(name,"position")){
                    if(!ctx->vertex)sc_diag_add(ctx->diags,SC_DIAG_ERROR,expr->line,1,"position is available only in vertex stage");
                    sc_strbuf_append(out,"material.worldPosition.xyz");
                }
            } else {
                sc_diag_add(ctx->diags, SC_DIAG_ERROR, expr->line, 1,
                            "use of undeclared identifier '%s'", expr->name);
                ctx->had_semantic_error = 1;
                sc_strbuf_append(out, expr->name);
            }
            break;
        case SC_EXPR_CALL:
            sc_emit_call(ctx, expr, pre_out, out);
            break;
        case SC_EXPR_BINARY:
            sc_strbuf_append(out, "(");
            sc_emit_expr(ctx, expr->left, pre_out, out);
            sc_strbuf_appendf(out, " %s ", expr->op);
            sc_emit_expr(ctx, expr->right, pre_out, out);
            sc_strbuf_append(out, ")");
            break;
        case SC_EXPR_UNARY:
            sc_strbuf_appendf(out, "%s", expr->op);
            sc_emit_expr(ctx, expr->left, pre_out, out);
            break;
        case SC_EXPR_MEMBER:
            sc_emit_expr(ctx, expr->object, pre_out, out);
            sc_strbuf_appendf(out, ".%s", expr->member);
            break;
        case SC_EXPR_INDEX:
            sc_emit_expr(ctx,expr->object,pre_out,out);sc_strbuf_append(out,"[");sc_emit_expr(ctx,expr->right,pre_out,out);sc_strbuf_append(out,"]");break;
    }
    --ctx->expression_depth;
}

/* ---- statement codegen ---- */

static void sc_emit_stmts(sc_gen_ctx* ctx, sc_stmt_list* stmts, sc_strbuf* out, int indent);

static const char* sc_lvalue_root(sc_expr* expr){
    while(expr&&(expr->kind==SC_EXPR_MEMBER||expr->kind==SC_EXPR_INDEX))expr=expr->object;
    return expr&&expr->kind==SC_EXPR_IDENTIFIER?expr->name:NULL;
}

static void sc_emit_lvalue(sc_gen_ctx* ctx,sc_expr* expr,sc_strbuf* out);
static void sc_emit_lvalue_inner(sc_gen_ctx* ctx,sc_expr* expr,sc_strbuf* out){
    if(!expr)return;
    if(expr->kind==SC_EXPR_MEMBER){sc_emit_lvalue(ctx,expr->object,out);sc_strbuf_appendf(out,".%s",expr->member);return;}
    if(expr->kind==SC_EXPR_INDEX){sc_strbuf pre;sc_strbuf_init(&pre);sc_emit_lvalue(ctx,expr->object,out);sc_strbuf_append(out,"[");sc_emit_expr(ctx,expr->right,&pre,out);if(pre.length)sc_diag_add(ctx->diags,SC_DIAG_ERROR,expr->line,1,"statement-emitting transforms cannot be used as assignment indices");sc_strbuf_append(out,"]");sc_strbuf_free(&pre);return;}
    const char* name=expr->name;
    if(sc_strset_contains(ctx->properties,name)){sc_diag_add(ctx->diags,SC_DIAG_ERROR,expr->line,1,"cannot assign to property '%s'; properties are read-only inputs",name);return;}
    if(sc_strset_contains(ctx->locals,name)){sc_strbuf_append(out,name);return;}
    const char* field=NULL;
    if(ctx->vertex){for(size_t i=0;i<sizeof(SC_VERTEX_FIELDS)/sizeof(SC_VERTEX_FIELDS[0]);++i)if(!strcmp(name,SC_VERTEX_FIELDS[i].bshader_name))field=SC_VERTEX_FIELDS[i].filament_lvalue;}
    else field=sc_lookup_output_field(name);
    if(!field){sc_diag_add(ctx->diags,SC_DIAG_ERROR,expr->line,1,"assignment to undeclared variable '%s' or read-only stage input",name);return;}
    if(!strcmp(name,"UV"))ctx->use_uv=1;
    if(!strcmp(name,"vertexColor"))ctx->use_color=1;
    if(!strcmp(name,"normal"))ctx->use_normal=1;
    if(ctx->unlit&&(!strcmp(name,"normal")||!strcmp(name,"roughness")||!strcmp(name,"metallic")||!strcmp(name,"ao")))sc_diag_add(ctx->diags,SC_DIAG_ERROR,expr->line,1,"output '%s' requires lit shading",name);
    sc_strbuf_append(out,field);
}

static void sc_emit_lvalue(sc_gen_ctx* ctx,sc_expr* expr,sc_strbuf* out){
    if(++ctx->expression_depth>128){sc_diag_add(ctx->diags,SC_DIAG_ERROR,expr?expr->line:1,1,"assignment nesting limit exceeded");--ctx->expression_depth;return;}
    sc_emit_lvalue_inner(ctx,expr,out);
    --ctx->expression_depth;
}

static void sc_indent(sc_strbuf* out, int indent) {
    for (int i = 0; i < indent; i++) sc_strbuf_append(out, "    ");
}

static void sc_emit_stmt(sc_gen_ctx* ctx, sc_stmt* stmt, sc_strbuf* out, int indent) {
    switch (stmt->kind) {
        case SC_STMT_VAR_DECL: {
            if(!strncmp(stmt->decl_name,"sc_",3)||sc_strset_contains(ctx->properties,stmt->decl_name)||sc_is_builtin_identifier(stmt->decl_name)||sc_lookup_output_field(stmt->decl_name))sc_diag_add(ctx->diags,SC_DIAG_ERROR,stmt->line,1,"local name '%s' conflicts with a reserved input/output",stmt->decl_name);
            for(size_t i=ctx->scope_start;i<ctx->locals->count;++i)if(!strcmp(ctx->locals->items[i],stmt->decl_name))sc_diag_add(ctx->diags,SC_DIAG_ERROR,stmt->line,1,"duplicate local '%s' in the same scope",stmt->decl_name);
            if(stmt->decl_type==SC_PROP_TEXTURE2D)sc_diag_add(ctx->diags,SC_DIAG_ERROR,stmt->line,1,"textures must be declared as properties, not local sampler variables");
            sc_strbuf pre;
            sc_strbuf_init(&pre);
            sc_strbuf value;
            sc_strbuf_init(&value);
            if (stmt->decl_init) {
                sc_emit_expr(ctx, stmt->decl_init, &pre, &value);
            }
            if (pre.length > 0) sc_strbuf_append(out, pre.data);

            sc_strset_push(ctx->locals, stmt->decl_name);
            sc_indent(out, indent);
            sc_strbuf_appendf(out, "%s %s", sc_filament_param_type(stmt->decl_type), stmt->decl_name);
            if (stmt->decl_init) {
                sc_strbuf_appendf(out, " = %s", value.data ? value.data : "0.0");
            }
            sc_strbuf_append(out, ";\n");

            sc_strbuf_free(&pre);
            sc_strbuf_free(&value);
            break;
        }
        case SC_STMT_ASSIGN: {
            sc_strbuf pre;
            sc_strbuf_init(&pre);
            sc_strbuf value;
            sc_strbuf_init(&value);
            sc_emit_expr(ctx, stmt->assign_value, &pre, &value);
            if (pre.length > 0) sc_strbuf_append(out, pre.data);

            sc_indent(out, indent);
            sc_emit_lvalue(ctx,stmt->assign_lvalue,out);
            sc_strbuf_appendf(out," = %s;\n",value.data?value.data:"0.0");

            sc_strbuf_free(&pre);
            sc_strbuf_free(&value);
            break;
        }
        case SC_STMT_IF: {
            sc_strbuf pre;
            sc_strbuf_init(&pre);
            sc_strbuf cond;
            sc_strbuf_init(&cond);
            sc_emit_expr(ctx, stmt->if_cond, &pre, &cond);
            if (pre.length > 0) sc_strbuf_append(out, pre.data);

            sc_indent(out, indent);
            sc_strbuf_appendf(out, "if (%s) {\n", cond.data ? cond.data : "false");
            sc_emit_stmts(ctx, stmt->if_then, out, indent + 1);
            sc_indent(out, indent);
            sc_strbuf_append(out, "}\n");
            if (stmt->if_else) {
                sc_indent(out, indent);
                sc_strbuf_append(out, "else {\n");
                sc_emit_stmts(ctx, stmt->if_else, out, indent + 1);
                sc_indent(out, indent);
                sc_strbuf_append(out, "}\n");
            }

            sc_strbuf_free(&pre);
            sc_strbuf_free(&cond);
            break;
        }
        case SC_STMT_EXPR: {
            sc_strbuf pre;
            sc_strbuf_init(&pre);
            sc_strbuf value;
            sc_strbuf_init(&value);
            sc_emit_expr(ctx, stmt->expr, &pre, &value);
            if (pre.length > 0) sc_strbuf_append(out, pre.data);
            sc_indent(out, indent);
            sc_strbuf_appendf(out, "%s;\n", value.data ? value.data : "");
            sc_strbuf_free(&pre);
            sc_strbuf_free(&value);
            break;
        }
        case SC_STMT_FOR:
        case SC_STMT_WHILE: {
            size_t saved=ctx->locals->count;size_t scope=ctx->scope_start;ctx->scope_start=saved;
            unsigned int id=ctx->loop_id++;
            sc_indent(out,indent);sc_strbuf_append(out,"{\n");
            if(stmt->loop_init)sc_emit_stmt(ctx,stmt->loop_init,out,indent+1);
            sc_strbuf cond,pre,step;sc_strbuf_init(&cond);sc_strbuf_init(&pre);sc_strbuf_init(&step);
            sc_emit_expr(ctx,stmt->loop_cond,&pre,&cond);
            if(stmt->loop_step){const char* root=sc_lvalue_root(stmt->loop_step->assign_lvalue);if(!root||!sc_strset_contains(ctx->locals,root))sc_diag_add(ctx->diags,SC_DIAG_ERROR,stmt->line,1,"for step must modify a declared local");sc_emit_lvalue(ctx,stmt->loop_step->assign_lvalue,&step);sc_strbuf_append(&step," = ");sc_emit_expr(ctx,stmt->loop_step->assign_value,&pre,&step);}
            if(pre.length)sc_diag_add(ctx->diags,SC_DIAG_ERROR,stmt->line,1,"statement-emitting transforms cannot be used in loop headers");
            sc_indent(out,indent+1);sc_strbuf_appendf(out,"for (int sc_loop_%u = 0; sc_loop_%u < 1024 && (%s); ++sc_loop_%u",id,id,cond.data?cond.data:"false",id);
            if(step.length)sc_strbuf_appendf(out,", %s",step.data);
            sc_strbuf_append(out,") {\n");sc_emit_stmts(ctx,stmt->loop_body,out,indent+2);sc_indent(out,indent+1);sc_strbuf_append(out,"}\n");sc_indent(out,indent);sc_strbuf_append(out,"}\n");
            ctx->locals->count=saved;ctx->scope_start=scope;sc_strbuf_free(&cond);sc_strbuf_free(&pre);sc_strbuf_free(&step);break;
        }
        case SC_STMT_BREAK:sc_indent(out,indent);sc_strbuf_append(out,"break;\n");break;
        case SC_STMT_CONTINUE:sc_indent(out,indent);sc_strbuf_append(out,"continue;\n");break;
    }
}

static void sc_emit_stmts(sc_gen_ctx* ctx, sc_stmt_list* stmts, sc_strbuf* out, int indent) {
    size_t saved=ctx->locals->count,scope=ctx->scope_start;ctx->scope_start=saved;
    for (sc_stmt_list* it = stmts; it; it = it->next) {
        sc_emit_stmt(ctx, it->stmt, out, indent);
    }
    ctx->locals->count=saved;ctx->scope_start=scope;
}

/* ---- top level ---- */

static void sc_emit_parameter_default(sc_strbuf* out, const sc_property_decl* prop) {
    if (!prop->has_default) return;
    switch (prop->type) {
        case SC_PROP_BOOL:
            sc_strbuf_appendf(out, ", default : %s", prop->default_bool ? "true" : "false");
            break;
        case SC_PROP_FLOAT:
            sc_strbuf_appendf(out, ", default : %.9g", prop->default_number);
            break;
        case SC_PROP_INT:
            sc_strbuf_appendf(out, ", default : %.0f", prop->default_number);
            break;
        default:
            if(prop->type!=SC_PROP_TEXTURE2D){int count=prop->type==SC_PROP_VEC2?2:prop->type==SC_PROP_VEC3?3:prop->type==SC_PROP_VEC4?4:prop->type==SC_PROP_MAT3?9:16;
                sc_strbuf_append(out,", default : [");for(int i=0;i<count;++i)sc_strbuf_appendf(out,"%s%.9g",i?", ":"",prop->default_values[i]);sc_strbuf_append(out,"]");}
            break;
    }
}

char* sc_generate_filament_material(const sc_program* program,
                                     const sc_transform_registry* registry,
                                     sc_diag_list* diags,
                                     size_t* out_length) {
    if (!program || !program->shader) {
        if (out_length) *out_length = 0;
        return NULL;
    }

    const sc_shader_decl* shader = program->shader;

    sc_strset properties;
    sc_strset_init(&properties);
    sc_strset samplers;
    sc_strset_init(&samplers);
    for (sc_property_decl* p = shader->properties; p; p = p->next) {
        if(strlen(p->name)>200||!strncmp(p->name,"sc_",3)||sc_is_builtin_identifier(p->name)||sc_lookup_output_field(p->name))sc_diag_add(diags,SC_DIAG_ERROR,shader->line,1,"property '%s' conflicts with a reserved name or is too long",p->name);
        if(sc_strset_contains(&properties,p->name))sc_diag_add(diags,SC_DIAG_ERROR,shader->line,1,"duplicate property '%s'",p->name);
        sc_strset_add(&properties, p->name);
        if(p->type==SC_PROP_TEXTURE2D)sc_strset_add(&samplers,p->name);
    }

    sc_strset locals;
    sc_strset_init(&locals);

    sc_strset required_helpers;
    sc_strset_init(&required_helpers);

    sc_gen_ctx ctx;
    memset(&ctx,0,sizeof(ctx));ctx.unlit=shader->unlit;
    ctx.registry = registry;
    ctx.locals = &locals;
    ctx.properties = &properties;
    ctx.samplers = &samplers;
    ctx.required_helpers = &required_helpers;
    ctx.diags = diags;
    ctx.had_semantic_error = 0;

    /* -- material { } fragment body -- */
    sc_strbuf body;
    sc_strbuf_init(&body);
    sc_emit_stmts(&ctx, shader->material_body, &body, 2);
    sc_strset vertex_helpers;sc_strset_init(&vertex_helpers);ctx.required_helpers=&vertex_helpers;
    sc_strbuf vertex;sc_strbuf_init(&vertex);ctx.vertex=1;sc_emit_stmts(&ctx,shader->vertex_body,&vertex,2);ctx.vertex=0;
    if(ctx.unlit&&ctx.use_normal)sc_diag_add(diags,SC_DIAG_ERROR,shader->line,1,"normal input/output requires lit shading");

    /* -- assemble final output -- */
    sc_strbuf out;
    sc_strbuf_init(&out);

    sc_strbuf_appendf(&out, "// Generated by Bshader -- do not edit by hand.\n");
    sc_strbuf_appendf(&out, "material {\n");
    sc_strbuf_appendf(&out, "    name : \"%s\",\n", shader->name);
    sc_strbuf_appendf(&out, "    parameters : [\n");
    size_t param_index = 0;
    size_t param_count = 0;
    for (sc_property_decl* p = shader->properties; p; p = p->next) param_count++;
    for (sc_property_decl* p = shader->properties; p; p = p->next, param_index++) {
        sc_strbuf_appendf(&out, "        { type : %s, name : %s",
                           sc_filament_param_type(p->type), p->name);
        sc_emit_parameter_default(&out, p);
        sc_strbuf_appendf(&out, " }%s\n", (param_index + 1 < param_count) ? "," : "");
    }
    sc_strbuf_appendf(&out, "    ],\n");
    sc_strbuf_appendf(&out,"    requires : [%s%s%s],\n",ctx.use_uv?"uv0":"",ctx.use_color?(ctx.use_uv?", color":"color"):"",!shader->unlit?(ctx.use_uv||ctx.use_color?", tangents":"tangents"):"");
    sc_strbuf_appendf(&out, "    shadingModel : %s,\n",shader->unlit?"unlit":"lit");
    sc_strbuf_appendf(&out, "    doubleSided : %s,\n",shader->double_sided?"true":"false");
    sc_strbuf_appendf(&out, "    blending : %s\n",shader->transparent?"transparent":"opaque");
    sc_strbuf_appendf(&out, "}\n\n");

    sc_strbuf_appendf(&out, "fragment {\n");
    for (size_t i = 0; i < required_helpers.count; i++) {
        const sc_helper_entry* helper = sc_transform_registry_find_helper(registry, required_helpers.items[i]);
        if (helper) {
            sc_strbuf_append(&out, helper->source);
            sc_strbuf_append(&out, "\n");
        }else sc_diag_add(diags,SC_DIAG_ERROR,shader->line,1,"required helper '%s' is not registered",required_helpers.items[i]);
    }
    sc_strbuf_appendf(&out, "    void material(inout MaterialInputs material) {\n");
    sc_strbuf_appendf(&out, "        prepareMaterial(material);\n");
    sc_strbuf_append(&out, body.data ? body.data : "");
    if(ctx.use_normal)sc_strbuf_append(&out,"        prepareMaterial(material);\n");
    sc_strbuf_appendf(&out, "    }\n");
    sc_strbuf_appendf(&out, "}\n");
    if(shader->vertex_body){
        sc_strbuf_append(&out,"vertex {\n");
        for(size_t i=0;i<vertex_helpers.count;++i){const sc_helper_entry* helper=sc_transform_registry_find_helper(registry,vertex_helpers.items[i]);
            if(helper){sc_strbuf_append(&out,helper->source);sc_strbuf_append(&out,"\n");}else sc_diag_add(diags,SC_DIAG_ERROR,shader->line,1,"required vertex helper '%s' is not registered",vertex_helpers.items[i]);}
        sc_strbuf_append(&out,"    void materialVertex(inout MaterialVertexInputs material) {\n");
        sc_strbuf_append(&out,vertex.data?vertex.data:"");sc_strbuf_append(&out,"    }\n}\n");
    }

    sc_strbuf_free(&body);
    sc_strbuf_free(&vertex);
    sc_strset_free(&properties);
    sc_strset_free(&samplers);
    sc_strset_free(&locals);
    for (size_t i = 0; i < required_helpers.count; i++) free(required_helpers.items[i]);
    sc_strset_free(&required_helpers);
    for(size_t i=0;i<vertex_helpers.count;++i)free(vertex_helpers.items[i]);
    sc_strset_free(&vertex_helpers);

    if (out_length) *out_length = out.length;
    return sc_strbuf_take(&out);
}
