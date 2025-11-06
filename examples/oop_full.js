// Compiled by Mimo v0.1
console.log("=== OOP Feature Showcase ===");




class Creature {
  #energy;
  name = "Unknown";
  static totalCount = 0;

  constructor(name) {
    this.name = name;
    this.#energy = 100;
    Creature.totalCount = ( Creature.totalCount ) + ( 1 );
  }

  static count() {
    console.log("Total creatures: " + this.totalCount);
  }

  #rest(minutes) {
    this.#energy = ( this.#energy ) + ( minutes );
  }

  move(distance) {
    this.#energy = ( this.#energy ) - ( distance );
    console.log(this.name + " moves " + distance);
    return this.#energy;
  }

  eat(food) {
    console.log(this.name + " eats " + food);
    this.#rest(10);
  }

  sleep(hours) {
    console.log(this.name + " sleeps for " + hours + " hours");
    this.#rest(( hours ) * ( 10 ));
  }
}

class Lion extends Creature {
  static species = "Lion";

  roar() {
    console.log(this.name + " roars loudly!");
  }

  eat(food) {
    super.eat(food);
    console.log(this.name + " tears into the meat fiercely.");
  }
}

class Elephant extends Creature {
  static species = "Elephant";

  trumpet() {
    console.log(this.name + " trumpets proudly!");
  }

  eat(food) {
    console.log(this.name + " gently munches on " + food);
  }
}

function feedAll(animals, food) {
  for (const a of animals) {
    a.eat(food);
  }
}

function march(animals, distance) {
  for (const a of animals) {
    a.move(distance);
  }
}

function bedTime(animals, hours) {
  for (const a of animals) {
    a.sleep(hours);
  }
}

try {
  let simba = new Lion("Simba");
  let dumbo = new Elephant("Dumbo");
  let zoo = [simba, dumbo];

  simba.roar();
  dumbo.trumpet();

  Creature.count();

  march(zoo, 3);
  feedAll(zoo, "bananas");
  bedTime(zoo, 8);

  let energyLeft = simba.move(2);
  console.log("Simba's energy left: " + energyLeft);

  console.log("Second animal via indexing: " + (zoo[1]).name);
} catch (e) {
  console.log("Error: " + e);
} finally {
  console.log("=== End of OOP Feature Showcase ===");
}