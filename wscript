import Options
from os import unlink, symlink, popen
from os.path import exists, abspath
from shutil import copy

srcdir = '.'
blddir = 'build'
VERSION = '0.0.1'

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
  
def build(bld):
  mongo = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  mongo.cxxflags = "-g"
  mongo.target = 'mongo'
  mongo.source = "src/mongo.cc src/bson.cc"
  mongo.uselib = "MONGO BSON"

def shutdown():
  # HACK to get binding.node out of build directory.
  # better way to do this?
  if Options.commands['clean']:
    if exists('mongo.node'): unlink('mongo.node')
  else:
    if exists('build/default/mongo.node') and not exists('mongo.node'):
      copy('build/default/mongo.node', 'lib/mongo.node')
