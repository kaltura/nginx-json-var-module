#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

// constants
#define JSON_TIMESTAMP_LEN sizeof("2000-01-01T00:00:00Z")
#define JSON_TIMESTAMP_FORMAT "%Y-%m-%dT%H:%M:%SZ"

// typedefs
typedef struct {
	ngx_str_t name;
	ngx_http_complex_value_t cv;
} ngx_http_json_var_field_t;

typedef struct {
	ngx_str_t v;
	uintptr_t escape;
} ngx_http_json_var_value_t;

typedef struct {
	ngx_array_t fields;		// of ngx_http_json_var_field_t
	size_t base_json_size;
} ngx_http_json_var_ctx_t;

typedef struct {
	ngx_http_json_var_ctx_t* ctx;
	ngx_conf_t *cf;
} ngx_http_json_var_conf_ctx_t;

// forward decls
static char *ngx_http_json_var_json_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_json_var_add_variables(ngx_conf_t *cf);

// globals
static ngx_command_t ngx_http_json_var_commands[] = {

	{ ngx_string("json_var"),
	NGX_HTTP_MAIN_CONF | NGX_CONF_BLOCK | NGX_CONF_TAKE1,
	ngx_http_json_var_json_block,
	0,
	0,
	NULL },

    ngx_null_command
};

static ngx_http_module_t ngx_http_json_var_module_ctx = {
	ngx_http_json_var_add_variables,	/* preconfiguration */
    NULL,								/* postconfiguration */

    NULL,								/* create main configuration */
    NULL,								/* init main configuration */

    NULL,								/* create server configuration */
    NULL,								/* merge server configuration */

    NULL,								/* create location configuration */
    NULL								/* merge location configuration */
};

ngx_module_t ngx_http_json_var_module = {
    NGX_MODULE_V1,
    &ngx_http_json_var_module_ctx,		/* module context */
    ngx_http_json_var_commands,			/* module directives */
    NGX_HTTP_MODULE,					/* module type */
    NULL,								/* init master */
    NULL,								/* init module */
    NULL,								/* init process */
    NULL,								/* init thread */
    NULL,								/* exit thread */
    NULL,								/* exit process */
    NULL,								/* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_str_t time_var_name = ngx_string("json_time_gmt");

static ngx_int_t
ngx_http_json_set_time_var(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{
	struct tm tm;
	time_t now;

	now = ngx_time();
	ngx_libc_gmtime(now, &tm);

	v->data = ngx_pnalloc(r->pool, JSON_TIMESTAMP_LEN);
	if (v->data == NULL)
	{
		return NGX_ERROR;
	}

	v->len = strftime(
		(char*)v->data, 
		JSON_TIMESTAMP_LEN,
		(char *)JSON_TIMESTAMP_FORMAT, 
		&tm);
	if (v->len == 0)
	{
		return NGX_ERROR;
	}

	v->valid = 1;
	v->no_cacheable = 1;
	v->not_found = 0;

	return NGX_OK;
}

static ngx_int_t
ngx_http_json_var_add_variables(ngx_conf_t *cf)
{
	ngx_http_variable_t  *var;

	var = ngx_http_add_variable(cf, &time_var_name, NGX_HTTP_VAR_NOCACHEABLE);
	if (var == NULL)
	{
		return NGX_ERROR;
	}

	var->get_handler = ngx_http_json_set_time_var;
	return NGX_OK;
}

static char *
ngx_http_json_var_json(ngx_conf_t *cf, ngx_command_t *dummy, void *conf)
{
	ngx_http_compile_complex_value_t ccv;
	ngx_http_json_var_field_t* item;
	ngx_http_json_var_conf_ctx_t* conf_ctx;
	ngx_str_t *value;

	conf_ctx = cf->ctx;

	value = cf->args->elts;

	if (cf->args->nelts != 2) 
	{
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
			"invalid number of parameters");
		return NGX_CONF_ERROR;
	}

	item = ngx_array_push(&conf_ctx->ctx->fields);
	if (item == NULL)
	{
		return NGX_CONF_ERROR;
	}

	ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

	ccv.cf = conf_ctx->cf;
	ccv.value = &value[1];
	ccv.complex_value = &item->cv;

	if (ngx_http_compile_complex_value(&ccv) != NGX_OK)
	{
		return NGX_CONF_ERROR;
	}

	item->name = value[0];

	return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_json_var_variable(
	ngx_http_request_t *r, 
	ngx_http_variable_value_t *v,
	uintptr_t data)
{
	ngx_http_json_var_field_t* fields;
	ngx_http_json_var_value_t* values;
	ngx_http_json_var_ctx_t* ctx = (ngx_http_json_var_ctx_t *)data;
	ngx_uint_t i;
	size_t size;
	u_char* p;

	// allocate the values array
	values = ngx_palloc(r->pool, sizeof(values[0]) * ctx->fields.nelts);
	if (values == NULL)
	{
		return NGX_ERROR;
	}

	// evaluate the complex values
	fields = ctx->fields.elts;
	size = ctx->base_json_size;

	for (i = 0; i < ctx->fields.nelts; i++)
	{
		if (ngx_http_complex_value(r, &fields[i].cv, &values[i].v) != NGX_OK)
		{
			return NGX_ERROR;
		}

		values[i].escape = ngx_escape_json(NULL, values[i].v.data, values[i].v.len);

		size += values[i].v.len + values[i].escape;
	}

	// allocate the result size
	p = ngx_palloc(r->pool, size);
	if (p == NULL)
	{
		return NGX_ERROR;
	}

	// build the result
	v->data = p;

	*p++ = '{';
	i = 0;
	for (;;)
	{
		*p++ = '"';
		p = ngx_copy(p, fields[i].name.data, fields[i].name.len);
		*p++ = '"';
		*p++ = ':';
		*p++ = '"';
		if (values[i].escape)
		{
			p = (u_char*)ngx_escape_json(p, values[i].v.data, values[i].v.len);
		}
		else
		{
			p = ngx_copy(p, values[i].v.data, values[i].v.len);
		}
		*p++ = '"';

		i++;
		if (i >= ctx->fields.nelts)
		{
			break;
		}
		*p++ = ',';
	}
	*p++ = '}';
	*p = '\0';

	v->valid = 1;
	v->no_cacheable = 0;
	v->not_found = 0;
	v->len = p - v->data;

	if (v->len >= size)
	{
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
			"result length %uD exceeded allocated length %uz", (uint32_t)v->len, size);
		return NGX_ERROR;
	}

	return NGX_OK;
}

static char *
ngx_http_json_var_json_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
	ngx_http_json_var_conf_ctx_t conf_ctx;
	ngx_http_json_var_field_t* fields;
	ngx_http_json_var_ctx_t *ctx;
	ngx_http_variable_t *var;
	ngx_conf_t save;
	ngx_uint_t i;
	ngx_str_t *value;
	ngx_str_t name;
	char *rv;

	value = cf->args->elts;

	// get the variable name
	name = value[1];

	if (name.data[0] != '$') 
	{
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
			"invalid variable name \"%V\"", &name);
		return NGX_CONF_ERROR;
	}

	name.len--;
	name.data++;

	// initialize the context
	ctx = ngx_pcalloc(cf->pool, sizeof(*ctx));
	if (ctx == NULL)
	{
		return NGX_CONF_ERROR;
	}

	if (ngx_array_init(&ctx->fields, cf->pool, 10, sizeof(ngx_http_json_var_field_t)) != NGX_OK)
	{
		return NGX_CONF_ERROR;
	}

	// add the variable
	var = ngx_http_add_variable(cf, &name, NGX_HTTP_VAR_CHANGEABLE | NGX_HTTP_VAR_NOCACHEABLE);
	if (var == NULL) 
	{
		return NGX_CONF_ERROR;
	}

	// parse the block
	var->get_handler = ngx_http_json_var_variable;
	var->data = (uintptr_t)ctx;

	conf_ctx.cf = &save;
	conf_ctx.ctx = ctx;

	save = *cf;
	cf->ctx = &conf_ctx;
	cf->handler = ngx_http_json_var_json;

	rv = ngx_conf_parse(cf, NULL);

	*cf = save;

	if (rv != NGX_CONF_OK) 
	{
		return rv;
	}

	if (ctx->fields.nelts <= 0)
	{
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
			"no fields defined in \"json_var\" block");
		return NGX_CONF_ERROR;
	}

	// get the base json size
	ctx->base_json_size = sizeof("{}");

	fields = ctx->fields.elts;
	for (i = 0; i < ctx->fields.nelts; i++)
	{
		ctx->base_json_size += sizeof("\"\":\"\",") + fields[i].name.len;
	}

	return rv;
}
