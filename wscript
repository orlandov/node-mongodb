import Options
from os import unlink, symlink, popen
from os.path import exists, abspath

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
  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.target = 'bson'
  obj.source = "bson.cc"
  obj.uselib = "BSON MONGO"

def shutdown():
  # HACK to get binding.node out of build directory.
  # better way to do this?
  if Options.commands['clean']:
    if exists('bson.node'): unlink('bson.node')
  else:
    if exists('build/default/bson.node') and not exists('bson.node'):
      symlink('build/default/bson.node', 'bson.node')
