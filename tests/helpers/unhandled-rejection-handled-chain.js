async function runHandledRejectCases() {
    async function awaitPromiseReject() {
        try {
            await Promise.reject(new Error('await-promise-reject'));
        } catch {
            // expected
        }
    }

    async function awaitAsyncThrow() {
        try {
            await (async() => {
                throw new Error('await-async-throw');
            })();
        } catch {
            // expected
        }
    }

    async function thenOnRejectedPromiseReject() {
        await Promise.reject(new Error('then-onRejected-promise-reject')).then(
            () => {
                throw new Error('should not resolve');
            },
            () => {
                // expected
            }
        );
    }

    async function captureAsyncReject() {
        const result = await Promise.resolve()
            .then(() => {
                return (async() => {
                    throw new Error('async-dependency-failure');
                })();
            })
            .then(
                value => ({ ok: true, value }),
                error => ({ ok: false, error })
            );

        if (result.ok !== false) {
            throw new Error('expected rejection result');
        }
    }

    await awaitPromiseReject();
    await awaitAsyncThrow();
    await thenOnRejectedPromiseReject();
    await captureAsyncReject();
}

const unexpected = [];
process.on('unhandledRejection', error => {
    unexpected.push(error?.message ?? String(error));
});

await runHandledRejectCases();
await new Promise(resolve => setTimeout(resolve, 25));

if (unexpected.length !== 0) {
    throw new Error(`unexpected unhandledRejection events: ${unexpected.join(',')}`);
}
