import getopts from 'getopts';

function getoptsCompat(argv, opts) {
    return getopts(argv, opts);
}

export { getoptsCompat as getopts };
export default getoptsCompat;
