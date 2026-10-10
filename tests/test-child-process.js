import { spawn, spawnSync, execSync } from "node:child_process";

const testExecSync = async () => {
    console.log("Testing execSync...");
    
    const result = execSync("echo 'exec sync test'");
    const output = result.toString().trim();
    console.log("execSync result:", output);
    
    return output.includes("exec sync test");
};

const testExecSyncWithOptions = async () => {
    console.log("\nTesting execSync with options...");
    
    const result = execSync("echo working", { cwd: "/" });
    const output = result.toString().trim();
    console.log("execSync with cwd:", output);
    
    return output.includes("working");
};

const testSpawnBasic = () => {
    console.log("\nTesting spawn basic...");
    
    const proc = spawn(["echo", "hello"]);
    console.log("spawn pid:", proc.pid);
    
    return new Promise((resolve) => {
        proc.wait().then(status => {
            console.log("spawn exit status:", status);
            resolve(status.exit_status === 0);
        });
    });
};

const testSpawnWithPipe = async () => {
    console.log("\nTesting spawn with pipe...");
    
    const proc = spawn(["echo", "hello"], { stdout: "pipe", stderr: "pipe" });
    console.log("spawn pipe pid:", proc.pid);
    
    return new Promise((resolve) => {
        proc.wait().then(status => {
            console.log("spawn pipe exit status:", status);
            resolve(status.exit_status === 0);
        });
    });
};

const testSpawnKill = async () => {
    console.log("\nTesting spawn with kill...");
    
    const proc = spawn(["sleep", "10"], {
        stdout: "ignore",
        stderr: "ignore"
    });
    
    console.log("started sleep process, pid:", proc.pid);
    
    setTimeout(() => {
        proc.kill();
        console.log("killed process");
    }, 100);
    
    return new Promise((resolve) => {
        proc.wait().then(status => {
            console.log("process killed, exit status:", status);
            console.log("killed flag:", proc.killed);
            resolve(status.term_signal === "SIGTERM");
        });
    });
};

const testSpawnSyncBasic = async () => {
    console.log("\nTesting spawnSync basic...");
    
    const result = spawnSync(["echo", "test"]);
    console.log("spawnSync result:", result);
    console.log("spawnSync exit status:", result?.exit_status);
    
    return result && typeof result === "object";
};

const runAllTests = async () => {
    console.log("=== node:child_process tests ===\n");
    
    const results = [];
    
    results.push({ name: "execSync", pass: await testExecSync() });
    results.push({ name: "execSync with options", pass: await testExecSyncWithOptions() });
    results.push({ name: "spawn basic", pass: await testSpawnBasic() });
    results.push({ name: "spawn with pipe", pass: await testSpawnWithPipe() });
    results.push({ name: "spawnSync basic", pass: await testSpawnSyncBasic() });
    results.push({ name: "spawn kill", pass: await testSpawnKill() });
    
    console.log("\n=== Test Results ===");
    let passed = 0;
    let failed = 0;
    
    for (const r of results) {
        const status = r.pass ? "PASS" : "FAIL";
        console.log(`${status}: ${r.name}`);
        if (r.pass) passed++;
        else failed++;
    }
    
    console.log(`\nTotal: ${passed} passed, ${failed} failed`);
    process.exit(failed > 0 ? 1 : 0);
};

runAllTests();
