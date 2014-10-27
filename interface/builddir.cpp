/****************************************************************************************************************************
 Copyright 2013 Allied Telesis Labs Ltd. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************************************************************/

#include <buildsys.h>

#define CHECK_ARGUMENT(F,N,T)		if(!lua_is##T(L, N)) throw CustomException("" #F "() requires a " #T " as argument " #N );

#define CHECK_ARGUMENT_TYPE(F,N,T,V) CHECK_ARGUMENT(F,N,table) \
	lua_pushstring(L, #T); \
	lua_gettable(L, N); \
	if(!lua_isstring(L, -1) || strcmp(lua_tostring(L, -1), "yes") != 0) throw CustomException("" #F "() requires an object of type " #T " as argument " #N); \
	lua_pop(L, 1); \
	lua_pushstring(L, "data"); \
	lua_gettable(L, N); \
	if(!lua_islightuserdata(L, -1)) throw CustomException("" #F "() requires data of argument " #N " to contain something of type " #T ""); \
	T * V = (T *)lua_topointer(L, -1); \
	lua_pop(L, 1);

static char *absolute_path(BuildDir *d, const char *dir, bool allowDL=false)
{
	char *path = NULL;
	if (dir[0] == '/' || (allowDL && !strncmp(dir, "dl/", 3)))
		asprintf(&path, "%s", dir);
	else
		asprintf(&path, "%s/%s", d->getPath(), dir);
	return path;
}

static char *relative_path(BuildDir *d, const char *dir, bool allowDL=false)
{
	char *path = NULL;
	if (dir[0] == '/' || (allowDL && !strncmp(dir, "dl/", 3)))
		asprintf(&path, "%s", dir);
	else
		asprintf(&path, "%s/%s", d->getShortPath(), dir);
	return path;
}

static char *absolute_fetch_path(Package *P, const char *location)
{
	char *src_path = NULL;
	char *cwd = getcwd(NULL, 0);
	if(location[0] == '/' || !strncmp(location, "dl/", 3) || location[0] == '.')
	{
		asprintf(&src_path, "%s/%s", cwd, location);
	} else {
		asprintf(&src_path, "%s/package/%s/%s", cwd, P->getName().c_str(), location);
	}
	free(cwd);
	return src_path;
}

static char *relative_fetch_path(Package *P, const char *location)
{
	char *src_path = NULL;
	if(location[0] == '/' || !strncmp(location, "dl/", 3) || location[0] == '.')
	{
		src_path = strdup(location);
	} else {
		asprintf(&src_path, "package/%s/%s", P->getName().c_str(), location);
	}
	return src_path;
}

static void add_env(Package *P, PackageCmd *pc)
{
	char *pn_env = NULL;
	asprintf(&pn_env, "BS_PACKAGE_NAME=%s", P->getName().c_str());
	pc->addEnv(pn_env);
	free(pn_env);
}

int li_bd_fetch(lua_State *L)
{
	/* the first argument is the table, and is implicit */
	int argc = lua_gettop(L);
	if(argc < 1)
	{
		throw CustomException("fetch() requires at least 2 arguments");
	}
	if(!lua_istable(L, 1)) throw CustomException("fetch() must be called using : not .");
	if(!lua_isstring(L, 2)) throw CustomException("fetch() expects a string as the first argument");
	if(!lua_isstring(L, 3)) throw CustomException("fetch() expects a string as the second argument");

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	CHECK_ARGUMENT_TYPE("fetch",1,BuildDir,d);

	char *location = strdup(lua_tostring(L, 2));
	const char *method = lua_tostring(L, 3);

	char **argv = NULL;

	if(WORLD->forcedMode() && !WORLD->isForced(P->getName()))
	{
		return 0;
	}

	if(strcmp(method, "dl") == 0)
	{
		int res = mkdir("dl", 0700);
		if((res < 0) && (errno != EEXIST))
		{
			throw CustomException("Error: Creating download directory");
		}
		bool decompress = false;
		if(argc > 3)
		{
			decompress = lua_toboolean(L, 4);
		}
		bool get = true;
		char *fname = strrchr(location, '/');
		if(fname != NULL)
		{
			fname++;
			get = false;
			char *t = fname;
			if(decompress)
			{
				fname = strdup(fname);
				char *ext = strrchr(fname, '.');
				if(ext != NULL) ext[0] = '\0';
			}
			char *pwd = getcwd(NULL, 0);
			char *fpath = NULL;
			asprintf(&fpath, "%s/dl/%s", pwd, fname);
			free(pwd);
			FILE *f = fopen(fpath, "r");
			if(f == NULL)
			{
				get = true;
			} else {
				fclose(f);
			}
			free(fpath);
			if(decompress) free(fname);
			fname = t;
		}
		if(get)
		{
			argv = (char **)calloc(4, sizeof(char *));
			argv[0] = strdup("wget");
			argv[1] = strdup(location);
			if(run(P->getName().c_str(), (char *)"wget", argv , "dl", NULL) != 0)
				throw CustomException("Failed to fetch file");
			if(decompress)
			{
				// We want to run a command on this file
				char *cmd = NULL;
				char *ext = strrchr(fname, '.');
				if(ext == NULL)
				{
					log(P->getName().c_str(), (char *)"Could not guess decompression based on extension: %s\n", fname);
				}

				if(strcmp(ext, ".bz2")==0)
				{
					asprintf(&cmd, "bunzip2 -d dl/%s", fname);
				} else
				if(strcmp(ext, ".gz")==0)
				{
					asprintf(&cmd, "gunzip -d dl/%s", fname);
				}
				system(cmd);
				free(cmd);
			}
		}
	} else if(strcmp(method, "git") == 0) {
		argv = (char **)calloc(4, sizeof(char *));
		argv[0] = strdup("git");
		argv[1] = strdup("clone");
		argv[2] = strdup(location);
		if(run(P->getName().c_str(), (char *)"git", argv , d->getPath(), NULL) != 0)
			throw CustomException("Failed to git clone");
		fprintf(stderr, "Git clone, considering code updated\n");
		P->setCodeUpdated();
	} else if(strcmp(method, "linkgit") == 0) {
		char *l = relative_fetch_path(P, location);
		char *l2 = strrchr(l,'/');
		if(l2[1] == '\0')
		{
			l2[0] = '\0';
			l2 = strrchr(l,'/');
		}
		l2++;
		GitDirExtractionUnit *gdeu = new GitDirExtractionUnit(location,l2,true);
		P->extraction()->add(gdeu);
		free(l);
	} else if(strcmp(method, "link") == 0) {
		argv = (char **)calloc(5, sizeof(char *));
		argv[0] = strdup("ln");
		argv[1] = strdup("-sf");
		argv[2] = relative_fetch_path(P, location);
		argv[3] = strdup(".");
		if(run(P->getName().c_str(), (char *)"ln", argv , d->getPath(), NULL) != 0)
		{
			// An error occured, try remove the file, then relink
			char **rmargv = (char **)calloc(4, sizeof(char *));
			rmargv[0] = strdup("rm");
			rmargv[1] = strdup("-fr");
			char *l = strdup(argv[2]);
			char *l2 = strrchr(l,'/');
			if(l2[1] == '\0')
			{
				l2[0] = '\0';
				l2 = strrchr(l,'/');
			}
			l2++;
			rmargv[2] = strdup(l2);
			log(P->getName().c_str(), (char *)"%s %s %s\n", rmargv[0], rmargv[1], rmargv[2]);
			if(run(P->getName().c_str(), (char *)"/bin/rm", rmargv , d->getPath(), NULL) != 0)
				throw CustomException("Failed to ln (symbolically), could not remove target first");
			if(run(P->getName().c_str(), (char *)"ln", argv, d->getPath(), NULL) != 0)
				throw CustomException("Failed to ln (symbolically), even after removing target first");
			free(rmargv[0]);
			free(rmargv[1]);
			free(rmargv[2]);
			free(rmargv);
			free(l);
		}
		fprintf(stderr, "Linked data in, considering updated\n");
		P->setCodeUpdated();
	} else if(strcmp(method, "copyfile") == 0) {
		char *file_path = relative_fetch_path(P, location);
		P->extraction()->add(new FileCopyExtractionUnit(file_path));
		free(file_path);
	} else if(strcmp(method, "copygit") == 0) {
		char *src_path = NULL;
		src_path = relative_fetch_path(P, location);
		GitDirExtractionUnit *gdeu = new GitDirExtractionUnit(src_path,".",false);
		P->extraction()->add(gdeu);
	} else if(strcmp(method, "sm") == 0) {
		if (mkdir(d->getWorkBuild(), 0777) && errno != EEXIST)
		{
			// An error occured, try remove the direcotry, then recreate
			char **rmargv = (char **)calloc(4, sizeof(char *));
			rmargv[0] = strdup("rm");
			rmargv[1] = strdup("-fr");
			rmargv[2] = strdup(d->getWorkBuild());
			rmargv[3] = NULL;
			log(P->getName().c_str(), (char *)"%s %s %s\n", rmargv[0], rmargv[1], rmargv[2]);
			if(run(P->getName().c_str(), (char *)"/bin/rm", rmargv , d->getPath(), NULL) != 0)
				throw CustomException("Failed to mkdir, could not remove target first");
			if (mkdir(d->getWorkBuild(), 0777))
				throw CustomException("Failed to mkdir, even after removing target first");
			free(rmargv[0]);
			free(rmargv[1]);
			free(rmargv[2]);
			free(rmargv);
		}
		char *l = strdup(d->getWorkSrc());
		char *l2 = strrchr(l,'/');
		if(l2[1] == '\0')
		{
			l2[0] = '\0';
			l2 = strrchr(l,'/');
		}
		l2++;
		GitDirExtractionUnit *gdeu = new GitDirExtractionUnit(location,l2,true);
		P->extraction()->add(gdeu);
		free(l);
	} else if(strcmp(method, "copy") == 0) {
		argv = (char **)calloc(5, sizeof(char *));
		argv[0] = strdup("cp");
		argv[1] = strdup("-dpRuf");
		argv[2] = absolute_fetch_path(P, location);
		argv[3] = strdup(".");
		if(run(P->getName().c_str(), (char *)"cp", argv , d->getPath(), NULL) != 0)
			throw CustomException("Failed to copy (recursively)");
		fprintf(stderr, "Copied data in, considering code updated\n");
		P->setCodeUpdated();
	} else if(strcmp(method, "deps") == 0) {
		char *path = absolute_path(d, location);
		// record this directory (need to complete this operation later)
		lua_getglobal(L, "P");
		Package *P = (Package *)lua_topointer(L, -1);
		P->setDepsExtract(path);
		fprintf(stderr, "Will add installed files, considering code updated\n");
		P->setCodeUpdated();
	} else {
		throw CustomException("Unsupported fetch method");
	}

	free(location);

	if(argv != NULL)
	{
		int i = 0;
		while(argv[i] != NULL)
		{
			free(argv[i]);
			i++;
		}
		free(argv);
	}

	return 0;
}

int li_bd_restore(lua_State *L)
{
	/* the first argument is the table, and is implicit */
	int argc = lua_gettop(L);
	if(argc < 1)
	{
		throw CustomException("restore() requires at least 2 arguments");
	}
	if(!lua_istable(L, 1)) throw CustomException("restore() must be called using : not .");
	if(!lua_isstring(L, 2)) throw CustomException("restore() expects a string as the first argument");
	if(!lua_isstring(L, 3)) throw CustomException("restore() expects a string as the second argument");

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	CHECK_ARGUMENT_TYPE("restore",1,BuildDir,d);

	char *location = strdup(lua_tostring(L, 2));
	const char *method = lua_tostring(L, 3);

	if(WORLD->forcedMode() && !WORLD->isForced(P->getName()))
	{
		return 0;
	}

	if(strcmp(method, "copyfile") == 0) {
		PackageCmd *pc = new PackageCmd(d->getPath(), "cp");

		pc->addArg("cp");
		pc->addArg("-dpRuf");

		char const* fn = strrchr(location, '/');
		pc->addArg(fn != NULL ? fn + 1 : location);

		char *tmp = absolute_fetch_path(P, location);
		pc->addArg(tmp);
		free(tmp);

		P->addCommand(pc);
	} else {
		throw CustomException("Unsupported restore method");
	}

	free(location);

	return 0;
}

int li_bd_extract(lua_State *L)
{
	if(lua_gettop(L) != 2) throw CustomException("extract() requires exactly 1 argument");
	if(!lua_istable(L, 1)) throw CustomException("extract() must be called using : not .");
	if(!lua_isstring(L, 2)) throw CustomException("extract() expects a string as the only argument");

	lua_pushstring(L, "data");
	lua_gettable(L, 1);
	if(!lua_islightuserdata(L, -1)) throw CustomException("extract() must be called using : not .");
	BuildDir *d = (BuildDir *)lua_topointer(L, -1);
	lua_pop(L, 1);

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	if(WORLD->forcedMode() && !WORLD->isForced(P->getName()))
	{
		return 0;
	}

	const char *fName = lua_tostring(L, 2);
	char *realFName = relative_path(d, fName, true);

	ExtractionUnit *eu = NULL;
	if(strstr(fName, ".zip") != NULL)
	{
		eu = new ZipExtractionUnit(realFName);
	} else {
		// The catch all for tar compressed files
		eu = new TarExtractionUnit(realFName);
	}
	P->extraction()->add(eu);

	free(realFName);
	return 0;
}

int li_bd_cmd(lua_State *L)
{
	int argc = lua_gettop(L);
	if(argc < 4) throw CustomException("cmd() requires at least 3 arguments");
	if(argc > 5) throw CustomException("cmd() requires at most 4 arguments");
	if(!lua_istable(L, 1)) throw CustomException("cmd() must be called using : not .");
	if(!lua_isstring(L, 2)) throw CustomException("cmd() expects a string as the first argument");
	if(!lua_isstring(L, 3)) throw CustomException("cmd() expects a string as the second argument");
	if(!lua_istable(L, 4)) throw CustomException("cmd() expects a table of strings as the third argument");
	if(argc == 5 && !lua_istable(L, 5)) throw CustomException("cmd() expects a table of strings as the fourth argument, if present");

	CHECK_ARGUMENT_TYPE("cmd",1,BuildDir,d);

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	char *dir = relative_path(d, lua_tostring(L, 2));
	const char *app = lua_tostring(L, 3);

	PackageCmd *pc = new PackageCmd(dir, app);

	pc->addArg(app);

	lua_pushnil(L);  /* first key */
	while (lua_next(L, 4) != 0) {
		/* uses 'key' (at index -2) and 'value' (at index -1) */
		if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("cmd() requires a table of strings as the third argument\n");
		pc->addArg(lua_tostring(L, -1));
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}

	if(argc == 5)
	{
		lua_pushnil(L);  /* first key */
		while (lua_next(L, 5) != 0) {
			/* uses 'key' (at index -2) and 'value' (at index -1) */
			if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("cmd() requires a table of strings as the fourth argument\n");
			pc->addEnv(lua_tostring(L, -1));
			/* removes 'value'; keeps 'key' for next iteration */
			lua_pop(L, 1);
		}
	}

	add_env(P, pc);
	P->addCommand(pc);

	free(dir);

	return 0;
}

int li_bd_shell(lua_State *L)
{
	/* bd:shell(dir, command [, env]) */

	int argc = lua_gettop(L);
	if(argc < 3) throw CustomException("shell() requires at least 2 arguments");
	if(argc > 4) throw CustomException("shell() requires at most 3 arguments");
	if(!lua_istable(L, 1)) throw CustomException("shell() must be called using : not .");
	if(!lua_isstring(L, 2)) throw CustomException("shell() expects a string as the first argument");
	if(!lua_isstring(L, 3)) throw CustomException("shell() expects a string as the second argument");
	if(argc >= 4 && !lua_istable(L, 4)) throw CustomException("shell() expects a table of strings as the third argument, if present");

	CHECK_ARGUMENT_TYPE("shell",1,BuildDir,d);

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	char *path = absolute_path(d, lua_tostring(L, 2));
	std::string cmd = lua_tostring(L, 3);

	PackageCmd *pc = new PackageCmd(path, "bash");

	pc->addArg("bash");
	pc->addArg("-c");
	pc->addArg(cmd);

	if(argc == 4)
	{
		lua_pushnil(L);  /* first key */
		while (lua_next(L, 4) != 0) {
			/* uses 'key' (at index -2) and 'value' (at index -1) */
			if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("shell() requires a table of strings as the third argument\n");
			pc->addEnv(lua_tostring(L, -1));
			/* removes 'value'; keeps 'key' for next iteration */
			lua_pop(L, 1);
		}
	}

	add_env(P, pc);
	P->addCommand(pc);

	free(path);

	return 0;
}

int li_bd_autoreconf(lua_State *L)
{
	/* bd:autoreconf() */

	int argc = lua_gettop(L);
	if(argc != 1) throw CustomException("autoreconf() requires zero arguments");

	CHECK_ARGUMENT_TYPE("autoreconf",1,BuildDir,d);

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	PackageCmd *pc = new PackageCmd(d->getWorkSrc(), "autoreconf");

	if (WORLD->areSkipConfigure())
		pc->skipCommand();

	std::string incdir = d->getStaging();
	incdir += "/usr/local/aclocal";

	pc->addArg("autoreconf");
	pc->addArg("-i");
	pc->addArg("-B");
	pc->addArg(incdir);

	add_env(P, pc);
	P->addCommand(pc);

	return 0;
}

int li_bd_configure(lua_State *L)
{
	/* bd:configure(args [, env [, dir]]) */

	int argc = lua_gettop(L);
	if(argc < 2) throw CustomException("configure() requires at least 1 arguments");
	if(argc > 4) throw CustomException("configure() requires at most 3 arguments");
	if(!lua_istable(L, 1)) throw CustomException("configure() must be called using : not .");
	if(!lua_istable(L, 2)) throw CustomException("configure() expects a table of string as the first argument");
	if(argc >= 3 && !lua_istable(L, 3)) throw CustomException("configure() expects a table of strings as the second argument, if present");
	if(argc >= 4 && !lua_isstring(L, 4)) throw CustomException("configure() expects a string as the third argument, if present");

	CHECK_ARGUMENT_TYPE("configure",1,BuildDir,d);

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	std::string path = d->getWorkBuild();
	if (argc >= 4)
	{
		std::string dir = lua_tostring(L, 4);
		if (dir[0] == '/')
			path = dir;
		else
			path += "/" + dir;
	}

	std::string app = "../" + P->getName() + "/configure";

	PackageCmd *pc = new PackageCmd(path, app);

	if (WORLD->areSkipConfigure())
		pc->skipCommand();

	pc->addArg(app);

	lua_pushnil(L);  /* first key */
	while (lua_next(L, 2) != 0) {
		/* uses 'key' (at index -2) and 'value' (at index -1) */
		if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("configure() requires a table of strings as the first argument\n");
		pc->addArg(lua_tostring(L, -1));
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}

	if(argc >= 3)
	{
		lua_pushnil(L);  /* first key */
		while (lua_next(L, 3) != 0) {
			/* uses 'key' (at index -2) and 'value' (at index -1) */
			if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("configure() requires a table of strings as the second argument\n");
			pc->addEnv(lua_tostring(L, -1));
			/* removes 'value'; keeps 'key' for next iteration */
			lua_pop(L, 1);
		}
	}

	add_env(P, pc);
	P->addCommand(pc);


	return 0;
}

int li_bd_make(lua_State *L)
{
	/* bd:make(args [, env [,dir]]) */

	int argc = lua_gettop(L);
	if(argc < 2) throw CustomException("make() requires at least 1 arguments");
	if(argc > 4) throw CustomException("make() requires at most 3 arguments");
	if(!lua_istable(L, 1)) throw CustomException("make() must be called using : not .");
	if(!lua_istable(L, 2)) throw CustomException("make() expects a table of string as the first argument");
	if(argc >= 3 && !lua_istable(L, 3)) throw CustomException("make() expects a table of strings as the second argument, if present");
	if(argc >= 4 && !lua_isstring(L, 4)) throw CustomException("make() expects a string as the third argument, if present");

	CHECK_ARGUMENT_TYPE("make",1,BuildDir,d);

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	std::string path = d->getWorkBuild();
	if (argc >= 4)
	{
		std::string dir = lua_tostring(L, 4);
		if (dir[0] == '/')
			path = dir;
		else
			path += "/" + dir;
	}

	PackageCmd *pc = new PackageCmd(path, "make");

	pc->addArg("make");
	try {
		std::string value = "-j" + WORLD->getFeature("job-limit");
		pc->addArg(value);
	}
	catch(NoKeyException &E) {
	}
	try {
		std::string value = "-l" + WORLD->getFeature("load-limit");
		pc->addArg(value);
	}
	catch(NoKeyException &E) {
	}

	lua_pushnil(L);  /* first key */
	while (lua_next(L, 2) != 0) {
		/* uses 'key' (at index -2) and 'value' (at index -1) */
		if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("make() requires a table of strings as the first argument\n");
		pc->addArg(lua_tostring(L, -1));
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}

	if(argc >= 3)
	{
		lua_pushnil(L);  /* first key */
		while (lua_next(L, 3) != 0) {
			/* uses 'key' (at index -2) and 'value' (at index -1) */
			if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("make() requires a table of strings as the second argument\n");
			pc->addEnv(lua_tostring(L, -1));
			/* removes 'value'; keeps 'key' for next iteration */
			lua_pop(L, 1);
		}
	}

	add_env(P, pc);
	P->addCommand(pc);

	return 0;
}


int li_bd_patch(lua_State *L)
{
	if(lua_gettop(L) != 4) throw CustomException("patch() requires exactly 3 arguments");
	if(!lua_istable(L, 1)) throw CustomException("patch() must be called using : not .");
	if(!lua_isstring(L, 2)) throw CustomException("patch() expects a string as the first argument");
	if(!lua_isnumber(L, 3)) throw CustomException("patch() expects a number as the second argument");
	if(!lua_istable(L, 4)) throw CustomException("patch() expects a table of strings as the third argument");

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	if(WORLD->forcedMode() && !WORLD->isForced(P->getName()))
	{
		return true;
	}

	CHECK_ARGUMENT_TYPE("patch",1,BuildDir,d);

	char *patch_path = relative_path (d, lua_tostring(L, 2), true);

	int patch_depth = lua_tonumber(L, 3);


	lua_pushnil(L);  /* first key */
	while (lua_next(L, 4) != 0) {
		/* uses 'key' (at index -2) and 'value' (at index -1) */
		{
			char *uri = NULL;
			if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("patch() requires a table of strings as the third argument\n");
			uri = relative_fetch_path(P, lua_tostring(L, -1));
			PatchExtractionUnit *peu = new PatchExtractionUnit(patch_depth, patch_path, uri);
			P->extraction()->add(peu);
			free(uri);
		}
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}

	free(patch_path);

	return 0;
}

int li_bd_installfile(lua_State *L)
{
	if(lua_gettop(L) != 2) throw CustomException("installfile() requires exactly 1 argument");
	if(!lua_istable(L, 1)) throw CustomException("installfile() must be called using : not .");
	if(!lua_isstring(L, 2)) throw CustomException("installfile() expects a string as the only argument");

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	P->setInstallFile(strdup(lua_tostring(L, 2)));

	return 0;
}

int li_bd_invokebuild(lua_State *L)
{
	int args = lua_gettop(L);
	if(args != 4)
	{
		throw CustomException("invokebuild() takes 3 parameters");
	}

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	const char *target = lua_tostring(L, 2);
	std::string path = std::string(getcwd(NULL, 0));
	std::string buildsysexe = WORLD->getAppName();

	PackageCmd *pc = new PackageCmd(path, target);
	pc->addArg(buildsysexe);
	pc->addArg(target);

	// Deal with options to buildsys
	lua_pushnil(L);  /* first key */
	while (lua_next(L, 3) != 0) {
		/* uses 'key' (at index -2) and 'value' (at index -1) */
		if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("invokebuild() requires a table of strings as the second argument\n");
		pc->addArg(lua_tostring(L, -1));
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}

	// Add our known arguments to this list
	// --clean
	if(WORLD->areCleaning())
	{
		pc->addArg("--clean");
	}
	// --skip-configure
	if(WORLD->areSkipConfigure())
	{
		pc->addArg("--skip-configure");
	}
	// --nop
	if(!WORLD->areOutputPrefix())
	{
		pc->addArg("--nop");
	}
	// fetch from
	if(WORLD->canFetchFrom())
	{
		pc->addArg("--ff");
		pc->addArg(WORLD->fetchFrom());
	}
	// -- extract-only
	if(WORLD->areExtractOnly())
	{
		pc->addArg("--extract-only");
	}
	// our filter list
	if(WORLD->forcedMode())
	{
		WORLD->populateForcedList(pc);
	}

	// --, the seperator
	pc->addArg("--");

	lua_pushnil(L);  /* first key */
	while (lua_next(L, 4) != 0) {
		/* uses 'key' (at index -2) and 'value' (at index -1) */
		if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("invokebuild() requires a table of strings as the third argument\n");
		pc->addArg(lua_tostring(L, -1));
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}

	P->addCommand(pc);
	return 0;

}

int li_bd_mkdir(lua_State *L)
{
	int argc = lua_gettop(L);
	if(argc != 3) throw CustomException("mkdir() requires 2 arguments");
	if(!lua_istable(L, 1)) throw CustomException("mkdir() must be called using : not .");
	if(!lua_isstring(L, 2)) throw CustomException("mkdir() expects a string as the first argument");
	if(!lua_istable(L, 3)) throw CustomException("mkdir() expects a table of strings as the second argument");

	CHECK_ARGUMENT_TYPE("mkdir",1,BuildDir,d);

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	char *path = absolute_path(d, lua_tostring(L, 2));

	PackageCmd *pc = new PackageCmd(path, "mkdir");
	pc->addArg("mkdir");
	pc->addArg("-p");

	lua_pushnil(L);  /* first key */
	while (lua_next(L, 3) != 0) {
		/* uses 'key' (at index -2) and 'value' (at index -1) */
		if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("mkdir() requires a table of strings as the second argument\n");
		pc->addArg(lua_tostring(L, -1));
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}

	add_env(P, pc);
	P->addCommand(pc);

	free(path);

	return 0;
}

int li_bd_sed(lua_State *L)
{
	int argc = lua_gettop(L);
	if(argc != 4) throw CustomException("sed() requires 3 arguments");
	if(!lua_istable(L, 1)) throw CustomException("sed() must be called using : not .");
	if(!lua_isstring(L, 2)) throw CustomException("sed() expects a string as the first argument");
	if(!lua_isstring(L, 3)) throw CustomException("sed() expects a string as the second argument");
	if(!lua_istable(L, 4)) throw CustomException("sed() expects a table of strings as the third argument");

	CHECK_ARGUMENT_TYPE("sed",1,BuildDir,d);

	lua_getglobal(L, "P");
	Package *P = (Package *)lua_topointer(L, -1);

	char *path = absolute_path(d, lua_tostring(L, 2));

	PackageCmd *pc = new PackageCmd(path, "sed");
	pc->addArg("sed");
	pc->addArg("-i");
	pc->addArg("-e");
	pc->addArg(lua_tostring(L, 3));

	lua_pushnil(L);  /* first key */
	while (lua_next(L, 4) != 0) {
		/* uses 'key' (at index -2) and 'value' (at index -1) */
		if(lua_type(L, -1) != LUA_TSTRING) throw CustomException("sed() requires a table of strings as the third argument\n");
		pc->addArg(lua_tostring(L, -1));
		/* removes 'value'; keeps 'key' for next iteration */
		lua_pop(L, 1);
	}

	add_env(P, pc);
	P->addCommand(pc);

	free(path);

	return 0;
}
