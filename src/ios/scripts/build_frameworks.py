#encoding=utf-8
import sys
import os
import common
import argparse
import subprocess
import shutil

from pbxproj import XcodeProject
from pbxproj.pbxextensions.ProjectFiles import FileOptions
from pbxproj import *

FF_LIB_LIST = ['libavcodec.a','libavfilter.a','libavformat.a','libavutil.a','libswresample.a','libswscale.a']
FF_OPENSSL_LIST = ['libssl.a','libcrypto.a']

def remove_dependences(project):

    ret = True
    for lib in FF_LIB_LIST:
        files_ref = project.get_files_by_name(lib)
        for file in files_ref:
            ids = file.get_id()
            ret = project.remove_file_by_id(ids)
            common.logger.debug('remove lib [%s %s] ret %d'%(ids, lib, ret))
    
    for lib in FF_OPENSSL_LIST:
        files_ref = project.get_files_by_name(lib)
        for file in files_ref:
            ids = file.get_id()
            ret = project.remove_file_by_id(ids)
            common.logger.debug('remove lib [%s %s] ret %d'%(ids, lib, ret))
    
    return ret

def config_nossl_project(project):

    project.remove_header_search_paths(u'../build/universal/ffmpeg_3.1_openssl-1.1.1d-cc/include')
    project.remove_library_search_paths(u'../build/universal/ffmpeg_3.1_openssl-1.1.1d-cc/lib')

    project.add_header_search_paths(paths=u'../build/universal/nossl/include',recursive=False)
    project.add_library_search_paths(paths=u'../build/universal/nossl/lib',recursive=False)

    for lib in FF_LIB_LIST:
        lib_dir = '../build/universal/nossl/lib'
        lib_path = os.path.join(lib_dir, lib)
        print lib_path
        frameworks = project.get_or_create_group('Frameworks')
        file_options = FileOptions(weak=True)
        ret = project.add_file(lib_path, parent=frameworks, force=False, file_options=file_options)
        common.logger.debug('add lib [%s] ret %s'%(lib, ret))

    project.save()

def config_ssl_101i_project(project):
    project.remove_header_search_paths(u'../build/universal/ffmpeg_3.1_openssl-1.1.1d-cc/include')
    project.remove_library_search_paths(u'../build/universal/ffmpeg_3.1_openssl-1.1.1d-cc/lib')

    project.add_header_search_paths(paths=u'../build/universal/ffmpeg_3.1_openssl-1.0.1i-cc/include',recursive=False)
    project.add_library_search_paths(paths=u'../build/universal/ffmpeg_3.1_openssl-1.0.1i-cc/lib',recursive=False)
    project.save()

def config_ssl_111d_project(project):
    project.remove_header_search_paths(u'../build/universal/ffmpeg_3.1_openssl-1.0.1i-cc/include')
    project.remove_library_search_paths(u'../build/universal/ffmpeg_3.1_openssl-1.0.1i-cc/lib')

    project.add_header_search_paths(paths=u'../build/universal/ffmpeg_3.1_openssl-1.1.1d-cc/include',recursive=False)
    project.add_library_search_paths(paths=u'../build/universal/ffmpeg_3.1_openssl-1.1.1d-cc/lib',recursive=False)
    project.save()

def get_latest_version(branch):
     cmdstr = "git log -1 --pretty=format:'%h'"
     res = subprocess.check_output(cmdstr, shell=True)
     git_hash = res.split("\n")[0]
     return git_hash

def build_library(branch,tag):

    build_script = os.path.join(common.ROOT_PATH, 'build.sh')

    os.system('chmod +x %s'%(build_script))

    cmdstr = 'cd %s && sh %s %s %s'%(common.ROOT_PATH, build_script, branch, tag)

    common.logger.debug('cmdstr %s '%(cmdstr))

    build_result = os.system(cmdstr)
    if build_result != 0:
        return False

    return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser("cli")
    parser.add_argument('-branch', '--branch', type=str, help='branch info')
    args = parser.parse_args(sys.argv[1:])
    common.logger.debug('args %s'%(args))
    branch = args.branch.split("-")[0]
    common.logger.debug('current branch is %s '%(branch))

    frameworksOutputDir = os.path.join(common.ROOT_PATH, 'Release')
    common.logger.debug('----- clean outputDir %s ----'%(frameworksOutputDir))
    if os.path.exists(frameworksOutputDir):
        shutil.rmtree(frameworksOutputDir)

    #load pbxproj
    
    pbxprojPath = os.path.join(common.ROOT_PATH,'IJKMediaPlayer','IJKMediaPlayer.xcodeproj','project.pbxproj')
    common.logger.debug('pbxprojPath %s'%(pbxprojPath))
    # open the project
    project = XcodeProject.load(pbxprojPath)
    # backup the project
    backname = project.backup()
    common.logger.debug('project bak name %s'%(backname))

    #build universal ssl 1.1.1d
    common.logger.debug('----- build universal ssl 1.1.1d ----')
    ret = build_library(branch, 'universal_ssl_1.1.1d')
    common.logger.debug('----- build universal ssl 1.1.1d done %d----'%(ret))


    # #build unversal ssl 1.0.1i
    # common.logger.debug('----- build universal ssl 1.0.1i----')
    # config_ssl_101i_project(project)
    # ret = build_library(branch,'universal_ssl_1.0.1i')
    # common.logger.debug('----- build universal ssl 1.0.1i done %d----'%(ret))

    #build neox ssl
    common.logger.debug('----- build neox ssl----')
    ret = remove_dependences(project)
    if ret == True:
        project.save()
    else:
        common.logger.debug('build neox ssl failed as project modify fail.')
        sys.exit(1)
    ret = build_library(branch,'neox_ssl')
    common.logger.debug('----- build neox ssl done %d----'%(ret))


    #build unversal nossl
    common.logger.debug('----- build universal nossl ----')
    config_nossl_project(project)
    ret = build_library(branch,'universal_nossl')
    common.logger.debug('----- build universal nossl done %d----'%(ret))

    # recover
    old_project = XcodeProject.load(backname)
    old_project.save(pbxprojPath)
    os.remove(backname)
    common.logger.debug('----- restore pbxproj done ----')
