import Options
from os import unlink, symlink, popen
from os.path import exists, abspath, join as path_join

srcdir = '.'
blddir = 'build'
VERSION = '0.0.1'

mongo_install = "/home/orlando/mongo-local"
mongo_scripting = "../mongo/scripting"

def set_options(opt):
  opt.tool_options('compiler_cxx')

def configure(conf):
  conf.check_tool('compiler_cxx')
  conf.check_tool('node_addon')

  conf.env.append_value("LIBPATH_BSON", abspath("./mongo-c-driver/"))
  conf.env.append_value("LIB_BSON",     "bson")
  conf.env.append_value("CPPPATH_BSON", abspath("./mongo-c-driver/src"))

  conf.env.append_value("LIBPATH_MONGO", abspath("./mongo-c-driver/"))
  conf.env.append_value("LIB_MONGO",     "mongoc")
  conf.env.append_value("CPPPATH_MONGO", abspath("./mongo-c-driver/src"))

  conf.env.append_value("LIBPATH_MONGOCLIENT", path_join(mongo_install, "lib"))
  conf.env.append_value("LIB_MONGOCLIENT",     "mongoclient")
  conf.env.append_value("CPPPATH_MONGOCLIENT", path_join(mongo_install, "include"))

  conf.env.append_value("LIBPATH_BOOST_THREAD", "/usr/lib")
  conf.env.append_value("LIB_BOOST_THREAD",     "boost_thread")
  conf.env.append_value("CPPPATH_BOOST_THREAD", "/usr/include")

  conf.env.append_value("LIB_MONGOSCRIPTING",     "mongoclient")
  conf.env.append_value("CPPPATH_MONGOSCRIPTING", mongo_scripting)

def build(bld):
#   bson = bld.new_task_gen('cxx', 'shlib', 'node_addon')
#   bson.target = 'bson'
#   bson.source = "bson.cc"
#   bson.uselib = "BSON MONGO"

  mongo = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  mongo.target = 'mongo'
  mongo.source = "mongo.cc bson.cc %s %s" % (
    path_join(mongo_scripting, 'v8_utils.cpp'),
    path_join(mongo_scripting, 'v8_wrapper.cpp'),
  )

  mongo.uselib = "BOOST_THREAD MONGO BSON MONGOCLIENT MONGOSCRIPTING"

def shutdown():
  # HACK to get binding.node out of build directory.
  # better way to do this?
  if Options.commands['clean']:
    if exists('mongo.node'): unlink('mongo.node')
  else:
    if exists('build/default/mongo.node') and not exists('mongo.node'):
      symlink('build/default/mongo.node', 'mongo.node')
