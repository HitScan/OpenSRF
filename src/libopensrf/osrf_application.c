#include <opensrf/osrf_application.h>

static osrfMethod* _osrfAppBuildMethod( const char* methodName, const char* symbolName,
		const char* notes, int argc, int options, void* );
static void osrfAppSetOnExit(osrfApplication* app, const char* appName);
static int _osrfAppRegisterSysMethods( const char* app );
static osrfApplication* _osrfAppFindApplication( const char* name );
static osrfMethod* osrfAppFindMethod( osrfApplication* app, const char* methodName );
static int _osrfAppRespond( osrfMethodContext* context, const jsonObject* data, int complete );
static int _osrfAppPostProcess( osrfMethodContext* context, int retcode );
static int _osrfAppRunSystemMethod(osrfMethodContext* context);
static int osrfAppIntrospect( osrfMethodContext* ctx );
static int osrfAppIntrospectAll( osrfMethodContext* ctx );
static int osrfAppEcho( osrfMethodContext* ctx );

static osrfHash* _osrfAppHash = NULL;

int osrfAppRegisterApplication( const char* appName, const char* soFile ) {
	if(!appName || ! soFile) return -1;
	char* error;

	if(!_osrfAppHash) _osrfAppHash = osrfNewHash();

	osrfLogInfo( OSRF_LOG_MARK, "Registering application %s with file %s", appName, soFile );

	osrfApplication* app = safe_malloc(sizeof(osrfApplication));
	app->handle = dlopen (soFile, RTLD_NOW);
	app->onExit = NULL;

	if(!app->handle) {
		osrfLogWarning( OSRF_LOG_MARK, "Failed to dlopen library file %s: %s", soFile, dlerror() );
		dlerror(); /* clear the error */
		free(app);
		return -1;
	}

	app->methods = osrfNewHash();
	osrfHashSet( _osrfAppHash, app, appName );

	/* see if we can run the initialize method */
	int (*init) (void);
	*(void **) (&init) = dlsym(app->handle, "osrfAppInitialize");

	if( (error = dlerror()) != NULL ) {
		osrfLogWarning( OSRF_LOG_MARK, 
			"! Unable to locate method symbol [osrfAppInitialize] for app %s: %s", appName, error );

	} else {

		/* run the method */
		int ret;
		if( (ret = (*init)()) ) {
			osrfLogWarning( OSRF_LOG_MARK, "Application %s returned non-zero value from "
					"'osrfAppInitialize', not registering...", appName );
			//free(app->name); /* need a method to remove an application from the list */
			//free(app);
			return ret;
		}
	}

	_osrfAppRegisterSysMethods(appName);

	osrfLogInfo( OSRF_LOG_MARK, "Application %s registered successfully", appName );

	osrfLogSetAppname(appName);

	osrfAppSetOnExit(app, appName);

	return 0;
}


static void osrfAppSetOnExit(osrfApplication* app, const char* appName) {
	if(!(app && appName)) return;

	/* see if we can run the initialize method */
	char* error;
	void (*onExit) (void);
	*(void **) (&onExit) = dlsym(app->handle, "osrfAppChildExit");

	if( (error = dlerror()) != NULL ) {
		osrfLogDebug(OSRF_LOG_MARK, "No exit handler defined for %s", appName);
		return;
	}

	osrfLogInfo(OSRF_LOG_MARK, "registering exit handler for %s", appName);
	app->onExit = (*onExit);
}


int osrfAppRunChildInit(const char* appname) {
	osrfApplication* app = _osrfAppFindApplication(appname);
	if(!app) return -1;

	char* error;
	int ret;
	int (*childInit) (void);

	*(void**) (&childInit) = dlsym(app->handle, "osrfAppChildInit");

	if( (error = dlerror()) != NULL ) {
		osrfLogInfo( OSRF_LOG_MARK, "No child init defined for app %s : %s", appname, error);
		return 0;
	}

	if( (ret = (*childInit)()) ) {
		osrfLogError(OSRF_LOG_MARK, "App %s child init failed", appname);
		return -1;
	}

	osrfLogInfo(OSRF_LOG_MARK, "%s child init succeeded", appname);
	return 0;
}


void osrfAppRunExitCode() { 
	osrfHashIterator* itr = osrfNewHashIterator(_osrfAppHash);
	osrfApplication* app;
	while( (app = osrfHashIteratorNext(itr)) ) {
		if( app->onExit ) {
			osrfLogInfo(OSRF_LOG_MARK, "Running onExit handler for app %s",
				osrfHashIteratorKey(itr) );
			app->onExit();
		}
	}
	osrfHashIteratorFree(itr);
}


int osrfAppRegisterMethod( const char* appName, const char* methodName, 
		const char* symbolName, const char* notes, int argc, int options ) {

	return osrfAppRegisterExtendedMethod(
			appName,
			methodName,
			symbolName,
			notes,
			argc,
			options,
			NULL
	);

}

int osrfAppRegisterExtendedMethod( const char* appName, const char* methodName, 
		const char* symbolName, const char* notes, int argc, int options, void * user_data ) {

	if( !appName || ! methodName  ) return -1;

	osrfApplication* app = _osrfAppFindApplication(appName);
	if(!app) {
		osrfLogWarning( OSRF_LOG_MARK, "Unable to locate application %s", appName );
		return -1;
	}

	osrfLogDebug( OSRF_LOG_MARK, "Registering method %s for app %s", methodName, appName );

	osrfMethod* method = _osrfAppBuildMethod(
		methodName, symbolName, notes, argc, options, user_data );		
	method->options = options;

	/* plug the method into the list of methods */
	osrfHashSet( app->methods, method, method->name );

	if( options & OSRF_METHOD_STREAMING ) { /* build the atomic counterpart */
		int newops = options | OSRF_METHOD_ATOMIC;
		osrfMethod* atomicMethod = _osrfAppBuildMethod(
			methodName, symbolName, notes, argc, newops, NULL );		
		osrfHashSet( app->methods, atomicMethod, atomicMethod->name );
		atomicMethod->userData = method->userData;
	}

	return 0;
}



static osrfMethod* _osrfAppBuildMethod( const char* methodName, const char* symbolName,
		const char* notes, int argc, int options, void* user_data ) {

	osrfMethod* method					= safe_malloc(sizeof(osrfMethod));

	if(methodName) method->name		= strdup(methodName);
	else method->name    = NULL;
	if(symbolName) method->symbol		= strdup(symbolName);
	else method->symbol  = NULL;
	if(notes) method->notes				= strdup(notes);
	else method->notes   = NULL;
	if(user_data) method->userData	= user_data;

	method->argc							= argc;
	method->options						= options;

	if(options & OSRF_METHOD_ATOMIC) { /* add ".atomic" to the end of the name */
		char mb[strlen(method->name) + 8];
		sprintf(mb, "%s.atomic", method->name);
		free(method->name);
		method->name = strdup(mb);
		method->options |= OSRF_METHOD_STREAMING;
	}

	return method;
}


/**
  Registers all of the system methods for this app so that they may be
  treated the same as other methods */
static int _osrfAppRegisterSysMethods( const char* app ) {

	osrfAppRegisterMethod( 
			app, OSRF_SYSMETHOD_INTROSPECT, NULL, 
			"Return a list of methods whose names have the same initial "
			"substring as that of the provided method name PARAMS( methodNameSubstring )", 
			1, OSRF_METHOD_SYSTEM | OSRF_METHOD_STREAMING );

	osrfAppRegisterMethod( 
			app, OSRF_SYSMETHOD_INTROSPECT_ALL, NULL, 
			"Returns a complete list of methods. PARAMS()", 0, 
			OSRF_METHOD_SYSTEM | OSRF_METHOD_STREAMING );

	osrfAppRegisterMethod( 
			app, OSRF_SYSMETHOD_ECHO, NULL, 
			"Echos all data sent to the server back to the client. PARAMS([a, b, ...])", 0, 
			OSRF_METHOD_SYSTEM | OSRF_METHOD_STREAMING );

	return 0;
}

/**
  Finds the given app in the list of apps
  @param name The name of the application
  @return The application pointer or NULL if there is no such application
 */
static osrfApplication* _osrfAppFindApplication( const char* name ) {
	if(!name) return NULL;
	return (osrfApplication*) osrfHashGet(_osrfAppHash, name);
}

/**
  Finds the given method for the given app
  @param app The application object
  @param methodName The method to find
  @return A method pointer or NULL if no such method 
  exists for the given application
 */
static osrfMethod* osrfAppFindMethod( osrfApplication* app, const char* methodName ) {
	if(!app || ! methodName) return NULL;
	return (osrfMethod*) osrfHashGet( app->methods, methodName );
}

osrfMethod* _osrfAppFindMethod( const char* appName, const char* methodName ) {
	if(!appName || ! methodName) return NULL;
	return osrfAppFindMethod( _osrfAppFindApplication(appName), methodName );
}


int osrfAppRunMethod( const char* appName, const char* methodName, 
		osrfAppSession* ses, int reqId, jsonObject* params ) {

	if( !(appName && methodName && ses) ) return -1;

	char* error;
	osrfApplication* app;
	osrfMethod* method;
	osrfMethodContext context;

	context.session = ses;
	context.params = params;
	context.request = reqId;
	context.responses = NULL;

	/* this is the method we're gonna run */
	int (*meth) (osrfMethodContext*);	

	if( !(app = _osrfAppFindApplication(appName)) )
		return osrfAppRequestRespondException( ses, 
				reqId, "Application not found: %s", appName );
	
	if( !(method = osrfAppFindMethod( app, methodName )) )
		return osrfAppRequestRespondException( ses, reqId, 
				"Method [%s] not found for service %s", methodName, appName );

	context.method = method;

	#ifdef OSRF_STRICT_PARAMS
	if( method->argc > 0 ) {
		if(!params || params->type != JSON_ARRAY || params->size < method->argc )
			return osrfAppRequestRespondException( ses, reqId, 
				"Not enough params for method %s / service %s", methodName, appName );
	}
	#endif

	int retcode = 0;

	if( method->options & OSRF_METHOD_SYSTEM ) {
		retcode = _osrfAppRunSystemMethod(&context);

	} else {

		/* open and now run the method */
		*(void **) (&meth) = dlsym(app->handle, method->symbol);

		if( (error = dlerror()) != NULL ) {
			return osrfAppRequestRespondException( ses, reqId, 
				"Unable to execute method [%s]  for service %s", methodName, appName );
		}

		retcode = (*meth) (&context);
	}

	if(retcode < 0) 
		return osrfAppRequestRespondException( 
				ses, reqId, "An unknown server error occurred" );

	return _osrfAppPostProcess( &context, retcode );

}


int osrfAppRespond( osrfMethodContext* ctx, const jsonObject* data ) {
	return _osrfAppRespond( ctx, data, 0 );
}

int osrfAppRespondComplete( osrfMethodContext* context, const jsonObject* data ) {
	return _osrfAppRespond( context, data, 1 );
}

static int _osrfAppRespond( osrfMethodContext* ctx, const jsonObject* data, int complete ) {
	if(!(ctx && ctx->method)) return -1;

	if( ctx->method->options & OSRF_METHOD_ATOMIC ) {
		osrfLogDebug( OSRF_LOG_MARK,   
			"Adding responses to stash for atomic method %s", ctx->method->name );

		if( ctx->responses == NULL )
			ctx->responses = jsonNewObjectType( JSON_ARRAY );

		if ( data != NULL )
			jsonObjectPush( ctx->responses, jsonObjectClone(data) );	
	}


	if(	!(ctx->method->options & OSRF_METHOD_ATOMIC) && 
			!(ctx->method->options & OSRF_METHOD_CACHABLE) ) {

		if(complete) 
			osrfAppRequestRespondComplete( ctx->session, ctx->request, data );
		else
			osrfAppRequestRespond( ctx->session, ctx->request, data );
		return 0;
	}

	return 0;
}




static int _osrfAppPostProcess( osrfMethodContext* ctx, int retcode ) {
	if(!(ctx && ctx->method)) return -1;

	osrfLogDebug( OSRF_LOG_MARK,  "Postprocessing method %s with retcode %d",
			ctx->method->name, retcode );

	if(ctx->responses) { /* we have cached responses to return (no responses have been sent) */

		osrfAppRequestRespondComplete( ctx->session, ctx->request, ctx->responses );
		jsonObjectFree(ctx->responses);
		ctx->responses = NULL;

	} else {

		if( retcode > 0 ) 
			osrfAppSessionStatus( ctx->session, OSRF_STATUS_COMPLETE,  
					"osrfConnectStatus", ctx->request, "Request Complete" );
	}

	return 0;
}

int osrfAppRequestRespondException( osrfAppSession* ses, int request, const char* msg, ... ) {
	if(!ses) return -1;
	if(!msg) msg = "";
	VA_LIST_TO_STRING(msg);
	osrfLogWarning( OSRF_LOG_MARK,  "Returning method exception with message: %s", VA_BUF );
	osrfAppSessionStatus( ses, OSRF_STATUS_NOTFOUND, "osrfMethodException", request,  VA_BUF );
	return 0;
}


static void _osrfAppSetIntrospectMethod( osrfMethodContext* ctx, const osrfMethod* method, jsonObject* resp ) {
	if(!(ctx && resp)) return;

	jsonObjectSetKey(resp, "api_name",	jsonNewObject(method->name));
	jsonObjectSetKey(resp, "method",	jsonNewObject(method->symbol));
	jsonObjectSetKey(resp, "service",	jsonNewObject(ctx->session->remote_service));
	jsonObjectSetKey(resp, "notes",		jsonNewObject(method->notes));
	jsonObjectSetKey(resp, "argc",		jsonNewNumberObject(method->argc));

	jsonObjectSetKey(resp, "sysmethod", 
			jsonNewNumberObject( (method->options & OSRF_METHOD_SYSTEM) ? 1 : 0 ));
	jsonObjectSetKey(resp, "atomic",		
			jsonNewNumberObject( (method->options & OSRF_METHOD_ATOMIC) ? 1 : 0 ));
	jsonObjectSetKey(resp, "cachable",	
			jsonNewNumberObject( (method->options & OSRF_METHOD_CACHABLE) ? 1 : 0 ));
}

/**
  Trys to run the requested method as a system method.
  A system method is a well known method that all
  servers implement.  
  @param context The current method context
  @return 0 if the method is run successfully, return < 0 means
  the method was not run, return > 0 means the method was run
  and the application code now needs to send a 'request complete' 
  message
 */
static int _osrfAppRunSystemMethod(osrfMethodContext* ctx) {
	if( osrfMethodVerifyContext( ctx ) < 0 ) {
		osrfLogError( OSRF_LOG_MARK,  "_osrfAppRunSystemMethod: Received invalid method context" );
		return -1;
	}

	if(	!strcmp(ctx->method->name, OSRF_SYSMETHOD_INTROSPECT_ALL ) || 
			!strcmp(ctx->method->name, OSRF_SYSMETHOD_INTROSPECT_ALL_ATOMIC )) {

		return osrfAppIntrospectAll(ctx);
	}


	if(	!strcmp(ctx->method->name, OSRF_SYSMETHOD_INTROSPECT ) ||
			!strcmp(ctx->method->name, OSRF_SYSMETHOD_INTROSPECT_ATOMIC )) {

		return osrfAppIntrospect(ctx);
	}

	if(	!strcmp(ctx->method->name, OSRF_SYSMETHOD_ECHO ) ||
			!strcmp(ctx->method->name, OSRF_SYSMETHOD_ECHO_ATOMIC )) {

		return osrfAppEcho(ctx);
	}


	osrfAppRequestRespondException( ctx->session, 
			ctx->request, "System method implementation not found");

	return 0;
}


static int osrfAppIntrospect( osrfMethodContext* ctx ) {

	jsonObject* resp = NULL;
	const char* methodSubstring = jsonObjectGetString( jsonObjectGetIndex(ctx->params, 0) );
	osrfApplication* app = _osrfAppFindApplication( ctx->session->remote_service );
	int len = 0;

	if(!methodSubstring) return 1; /* respond with no methods */

	if(app) {

		osrfHashIterator* itr = osrfNewHashIterator(app->methods);
		osrfMethod* method;

		while( (method = osrfHashIteratorNext(itr)) ) {
			if( (len = strlen(methodSubstring)) <= strlen(method->name) ) {
				if( !strncmp( method->name, methodSubstring, len) ) {
					resp = jsonNewObject(NULL);
					_osrfAppSetIntrospectMethod( ctx, method, resp );
					osrfAppRespond(ctx, resp);
					jsonObjectFree(resp);
				}
			}
		}
		osrfHashIteratorFree(itr);
		return 1;
	}

	return -1;

}


static int osrfAppIntrospectAll( osrfMethodContext* ctx ) {
	jsonObject* resp = NULL;
	osrfApplication* app = _osrfAppFindApplication( ctx->session->remote_service );

	if(app) {
		osrfHashIterator* itr = osrfNewHashIterator(app->methods);
		osrfMethod* method;
		while( (method = osrfHashIteratorNext(itr)) ) {
			resp = jsonNewObject(NULL);
			_osrfAppSetIntrospectMethod( ctx, method, resp );
			osrfAppRespond(ctx, resp);
			jsonObjectFree(resp);
		}
		osrfHashIteratorFree(itr);
		return 1;
	}

	return -1;
}

static int osrfAppEcho( osrfMethodContext* ctx ) {
	if( osrfMethodVerifyContext( ctx ) < 0 ) {
		osrfLogError( OSRF_LOG_MARK,  "osrfAppEcho: Received invalid method context" );
		return -1;
	}
	
	int i;
	for( i = 0; i < ctx->params->size; i++ ) {
		const jsonObject* str = jsonObjectGetIndex(ctx->params,i);
		osrfAppRespond(ctx, str);
	}
	return 1;
}

/**
  Determine whether the context looks healthy.
  Return 0 if it does, or -1 if it doesn't.
 */
int osrfMethodVerifyContext( osrfMethodContext* ctx )
{
	if( !ctx ) {
		osrfLogError( OSRF_LOG_MARK,  "Context is NULL in app request" );
		return -1;
	}
	
	if( !ctx->session ) {
		osrfLogError( OSRF_LOG_MARK,  "Session is NULL in app request" );
		return -1;
	}

	if( !ctx->method )
	{
		osrfLogError( OSRF_LOG_MARK, "Method is NULL in app request" );
		return -1;
	}

	if( ctx->method->argc ) {
		if( !ctx->params ) {
			osrfLogError( OSRF_LOG_MARK,
				"Params is NULL in app request %s", ctx->method->name );
			return -1;
		}
		if( ctx->params->type != JSON_ARRAY ) {
			osrfLogError( OSRF_LOG_MARK,
				"'params' is not a JSON array for method %s", ctx->method->name );
			return -1;
		}
	}

	if( !ctx->method->name ) {
		osrfLogError( OSRF_LOG_MARK, "Method name is NULL" );
		 return -1;
	}

	// Log the call, with the method and parameters
	char* params_str = jsonObjectToJSON( ctx->params );
	if( params_str ) {
		osrfLogInfo( OSRF_LOG_MARK, "CALL:	%s %s - %s",
			 ctx->session->remote_service, ctx->method->name, params_str );
		free( params_str );
	}
	return 0;
}

