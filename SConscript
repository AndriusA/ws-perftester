Import('env')
Import('env_cpp11')
Import('boostlibs')
Import('platform_libs')
Import('polyfill_libs')
Import('tls_libs')

env = env.Clone ()
env_cpp11 = env_cpp11.Clone ()

prgs = []

# if a C++11 environment is available build using that, otherwise use boost
if env_cpp11.has_key('WSPP_CPP11_ENABLED'):
   ALL_LIBS = boostlibs(['system','thread'],env_cpp11) + [platform_libs] + [polyfill_libs]
   prgs += env_cpp11.Program('listener', ["listener.cpp"], LIBS = ALL_LIBS + tls_libs, LINKFLAGS="")
   prgs += env_cpp11.Program('listener-websocket', ["listener-websocket.cpp"], LIBS = ALL_LIBS + tls_libs, LINKFLAGS="")
   prgs += env_cpp11.Program('controller', ["controller.cpp"], LIBS = ALL_LIBS, LINKFLAGS="")
   prgs += env_cpp11.Program('plain-client', ["plain-client.cpp"], LIBS = ALL_LIBS + tls_libs, LINKFLAGS="")
else:
   ALL_LIBS = boostlibs(['system'],env) + [platform_libs] + [polyfill_libs]
   prgs += env.Program('listener', ["listener.cpp"], LIBS = ALL_LIBS, LINKFLAGS="")
   prgs += env.Program('listener-websocket', ["listener-websocket.cpp"], LIBS = ALL_LIBS + tls_libs, LINKFLAGS="")
   prgs += env.Program('controller', ["controller.cpp"], LIBS = ALL_LIBS, LINKFLAGS="")

Return('prgs')
