import os, pathlib, subprocess
base=pathlib.Path('/tmp/rollnw-edit-profile')
root=pathlib.Path('/home/josh/projects/rollnw/build/tools/client')
for name, executable, retained in [
    ('matched-before-1','rollnw-client-edit-matched-baseline',False),
    ('matched-after-1','rollnw-client',True),
    ('matched-after-2','rollnw-client',True),
    ('matched-before-2','rollnw-client-edit-matched-baseline',False),
]:
    env=os.environ.copy()
    env.update(XDG_DATA_HOME=str(base/'preferences'), ROLLNW_CLIENT_EDIT_TRACE=str(base/(name+'.jsonl')), ROLLNW_EDIT_REPLAY='1', ROLLNW_EDIT_NATIVE='1', ROLLNW_CLIENT_UNCAPPED='1')
    if retained: env['ROLLNW_EXPECT_RETAINED_ITEM']='1'
    if name.endswith('-1'): env['ROLLNW_EDIT_CAPTURE']='1'
    with (base/(name+'.log')).open('w') as log:
        result=subprocess.run([str(root/executable)],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    print(name, result.returncode, flush=True)
    result.check_returncode()
