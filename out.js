// Compiled by Mimo v0.1
    console.log("=== LukeLang Zoo Simulation ===");

    // [Mimo skipped] MUST METHOD eat WITH food

    // [Mimo skipped] MUST METHOD sleep WITH hours

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
