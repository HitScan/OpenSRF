#include <opensrf/osrf_app_session.h>
#include <opensrf/osrf_application.h>
#include <opensrf/osrf_json.h>
#include <opensrf/log.h>

#define MODULENAME "opensrf.dbmath"

int osrfAppInitialize();
int osrfAppChildInit();
int osrfMathRun( osrfMethodContext* );


int osrfAppInitialize() {

	osrfAppRegisterMethod( 
			MODULENAME, 
			"add", 
			"osrfMathRun", 
			"Addss two numbers", 2, 0 );

	osrfAppRegisterMethod( 
			MODULENAME, 
			"sub", 
			"osrfMathRun", 
			"Subtracts two numbers", 2, 0 );

	osrfAppRegisterMethod( 
			MODULENAME, 
			"mult", 
			"osrfMathRun", 
			"Multiplies two numbers", 2, 0 );

	osrfAppRegisterMethod( 
			MODULENAME, 
			"div", 
			"osrfMathRun", 
			"Divides two numbers", 2, 0 );

	return 0;
}

int osrfAppChildInit() {
	return 0;
}

int osrfMathRun( osrfMethodContext* ctx ) {
	if( osrfMethodVerifyContext( ctx ) ) {
		osrfLogError( OSRF_LOG_MARK,  "Invalid method context" );
		return -1;
	}

	const jsonObject* x = jsonObjectGetIndex(ctx->params, 0);
	const jsonObject* y = jsonObjectGetIndex(ctx->params, 1);

	if( x && y ) {

		char* a = jsonObjectToSimpleString(x);
		char* b = jsonObjectToSimpleString(y);

		if( a && b ) {

			double i = strtod(a, NULL);
			double j = strtod(b, NULL);
			double r = 0;

			if(!strcmp(ctx->method->name, "add"))	r = i + j;
			if(!strcmp(ctx->method->name, "sub"))	r = i - j;
			if(!strcmp(ctx->method->name, "mult"))	r = i * j;
			if(!strcmp(ctx->method->name, "div"))	r = i / j;

			jsonObject* resp = jsonNewNumberObject(r);
			osrfAppRespondComplete( ctx, resp );
			jsonObjectFree(resp);

			free(a); free(b);
			return 0;
		}
		else {
			if(a) free(a);
			if(b) free(b);
		}
	}

	return -1;
}



