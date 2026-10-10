"use strict";
import SerialPort from 'tjs:serial';

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

async function run() {
    const path = process.env.SERIAL_PATH || "/dev/cu.wchusbserial1130";
    const iterations = parseInt(process.env.SERIAL_ITERATIONS || "1000");

    console.log(`[mem-test] Starting: path=${path}, iterations=${iterations}`);

    const port = new SerialPort({
        path: path,
        baudRate: 115200,
        autoOpen: true,
    });

    let recvCount = 0;

    port.on("data", (data) => {
        recvCount++;
    });

    await sleep(500);

    const mem0 = tjs.engine.gc.get();
    console.log(`[mem-test] Initial:`);
    console.log(`  strCount=${mem0.strCount}, strSize=${mem0.strSize}`);
    console.log(`  objCount=${mem0.objCount}, objSize=${mem0.objSize}`);
    console.log(`  arrayCount=${mem0.arrayCount}, fastArrayCount=${mem0.fastArrayCount}`);
    console.log(`  jsFuncCount=${mem0.jsFuncCount}`);

    const payload = "AT+IOPWRREAD=VA\r\n";
    for (let i = 1; i <= iterations; i++) {
        await port.write(payload);
        await sleep(40);

        if (i % 200 === 0) {
            const mem = tjs.engine.gc.get();
            console.log(`[mem-test] iter=${i}:`);
            console.log(`  strCount=${mem.strCount}, strSize=${mem.strSize}`);
            console.log(`  objCount=${mem.objCount}, objSize=${mem.objSize}`);
            console.log(`  arrayCount=${mem.arrayCount}, fastArrayCount=${mem.fastArrayCount}`);
            console.log(`  jsFuncCount=${mem.jsFuncCount}`);
        }
    }

    const mem1 = tjs.engine.gc.get();
    console.log(`[mem-test] Final delta:`);
    console.log(`  strCount=${mem1.strCount - mem0.strCount}, strSize=${mem1.strSize - mem0.strSize}`);
    console.log(`  objCount=${mem1.objCount - mem0.objCount}, objSize=${mem1.objSize - mem0.objSize}`);
    console.log(`  arrayCount=${mem1.arrayCount - mem0.arrayCount}, fastArrayCount=${mem1.fastArrayCount - mem0.fastArrayCount}`);
    console.log(`  jsFuncCount=${mem1.jsFuncCount - mem0.jsFuncCount}`);

    await port.close();
    process.exit(0);
}

run().catch((error) => {
    console.log(`[mem-test] fatal: ${error.message}`);
    process.exit(2);
});