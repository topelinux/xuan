let handled = false;

process.on('unhandledRejection', (error) => {
    handled = error?.message === 'oops!';
    console.log(`handled:${error?.message}`);
});

(async() => {
    throw new Error('oops!');
})();

await new Promise(resolve => setTimeout(resolve, 20));

if (!handled) {
    throw new Error('unhandledRejection listener was not invoked');
}
