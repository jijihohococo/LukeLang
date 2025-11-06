// Compiled by Mimo v0.1
console.log("=== LukeLang Zoo Simulation ===");



class Animal {
  name;
  constructor(name) {
    this.name = name;
  }
  eat(food) {
    console.log(this.name + " eats " + food);
  }
  sleep(hours) {
    console.log(this.name + " sleeps for " + hours + " hours");
  }
  static {
    if (typeof this.prototype['eat'] !== 'function') { throw new Error('Class Animal must implement method eat from Eater'); }
    if (typeof this.prototype['sleep'] !== 'function') { throw new Error('Class Animal must implement method sleep from Sleeper'); }
  }
}

class Lion extends Animal {
  roar() {
    console.log(this.name + " roars loudly!");
  }
  eat(food) {
    super.eat(food);
    console.log(this.name + " tears into the meat fiercely.");
  }
}

class Elephant extends Animal {
  eat(food) {
    console.log(this.name + " gently munches on " + food);
  }
  trumpet() {
    console.log(this.name + " trumpets proudly!");
  }
}

let simba = new Lion("Simba");
let dumbo = new Elephant("Dumbo");

simba.roar();
simba.eat("antelope");
simba.sleep(5);

dumbo.trumpet();
dumbo.eat("bananas");
dumbo.sleep(8);

console.log("=== End of Zoo Simulation ===");
