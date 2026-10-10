import path from 'tjs:path';

const utf8Decoder = new TextDecoder();

export async function runScript(scriptPath, options = {}) {
    const args = [tjs.exePath, 'run', scriptPath];
    const proc = tjs.spawn(args, { stdout: 'pipe', stderr: 'pipe', ...options });
    const stdout = await proc.stdout.text();
    const stderr = await proc.stderr.text();
    const status = await proc.wait();
    
    return {
        stdout,
        stderr,
        exitStatus: status.exit_status
    };
}

export async function runAndParse(scriptPath, options = {}) {
    const result = await runScript(scriptPath, options);
    
    if (result.exitStatus !== 0) {
        throw new Error(`Script failed with exit status ${result.exitStatus}\nstdout: ${result.stdout}\nstderr: ${result.stderr}`);
    }
    
    try {
        return JSON.parse(result.stdout.trim());
    } catch (e) {
        throw new Error(`Failed to parse stdout as JSON: ${result.stdout}\nstderr: ${result.stderr}`);
    }
}

export function createTempFile(content, ext = '.js') {
    const tmpDir = path.join(tjs.cwd, 'tmp-test-' + Date.now());
    try {
        tjs.mkdirSync(tmpDir);
    } catch (e) {
        // ignore
    }
    const filePath = path.join(tmpDir, 'test' + ext);
    tjs.writeFileSync(filePath, content);
    return filePath;
}

export function cleanupTempFile(filePath) {
    try {
        tjs.unlinkSync(filePath);
        const dir = path.dirname(filePath);
        if (dir.includes('tmp-test-')) {
            tjs.rmdirSync(dir);
        }
    } catch (e) {
        // ignore
    }
}
