import os.path
import subprocess

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')


def build(ctx):
    script = os.path.join(ctx.path.abspath(), 'tools', 'build-settings.js')
    if subprocess.call(['node', script], cwd=ctx.path.abspath()) != 0:
        ctx.fatal('build-settings.js failed')

    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'),
                      target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf,
                             'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf, bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json']),
                   js_entry_file='src/pkjs/index.js')

    # The SDK bundles the webpack source map into the pbw (~88KB, ~half the
    # bundle) but nothing on the phone or watch ever reads it — strip it.
    def strip_source_map(ctx):
        import zipfile
        pbw = os.path.join(ctx.path.abspath(), 'build',
                           os.path.basename(ctx.path.abspath()) + '.pbw')
        if not os.path.exists(pbw):
            return
        with zipfile.ZipFile(pbw) as zin:
            infos = zin.infolist()
            items = [i for i in infos if not i.filename.endswith('.js.map')]
            if len(items) == len(infos):
                return
            data = {i.filename: zin.read(i.filename) for i in items}
        tmp = pbw + '.tmp'
        with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as zout:
            for i in items:
                zout.writestr(i, data[i.filename])
        os.replace(tmp, pbw)
        print('strip_source_map: removed .js.map from %s' % os.path.basename(pbw))
    ctx.add_post_fun(strip_source_map)
