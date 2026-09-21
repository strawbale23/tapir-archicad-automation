"""Reproducible CMake entry point; the SDK is supplied by the caller."""
import argparse,os,pathlib,shutil,subprocess,sys
p=argparse.ArgumentParser()
p.add_argument('--build',action='store_true');p.add_argument('--clean',action='store_true')
p.add_argument('--sdk',default=os.environ.get('AC_API_DEVKIT_DIR'))
p.add_argument('--cmake',default=shutil.which('cmake'))
p.add_argument('--version',type=int,choices=range(25,30),default=29)
p.add_argument('--generator',default='Visual Studio 17 2022' if os.name=='nt' else 'Unix Makefiles')
p.add_argument('--config',default='RelWithDebInfo')
a=p.parse_args()
if not a.cmake:p.error('CMake not found; supply --cmake.')
root=pathlib.Path(__file__).resolve().parent;build=root/f'archicad-addon/Build/AC{a.version}'
env={k.upper():v for k,v in os.environ.items()} if os.name=='nt' else dict(os.environ)
env['PATH']=str(pathlib.Path(sys.executable).parent)+os.pathsep+env.get('PATH','')
if a.build:
    args=[a.cmake,'--build',str(build),'--config',a.config,'--parallel','2']
    if a.clean:args.append('--clean-first')
else:
    if not a.sdk or not pathlib.Path(a.sdk).is_dir():p.error('Supply matching SDK Support directory with --sdk or AC_API_DEVKIT_DIR.')
    args=[a.cmake,'-S',str(root/'archicad-addon'),'-B',str(build),'-G',a.generator,f'-DAC_VERSION={a.version}','-DAC_API_DEVKIT_DIR='+str(pathlib.Path(a.sdk).resolve())]
    if os.name=='nt':args+=['-A','x64','-T','v143' if a.version==29 else 'v142']
sys.exit(subprocess.call(args,env=env))
