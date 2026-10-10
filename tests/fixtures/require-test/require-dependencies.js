const depA = require('./dependency-a');
console.log(JSON.stringify({ 
    depA: { 
        name: depA.name, 
        depBValue: depA.depBValue 
    },
    depAGetValueResult: depA.getValue()
}));
