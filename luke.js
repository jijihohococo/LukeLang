#!/usr/bin/env node

import { spawn } from 'child_process';
import { promisify } from 'util';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

function runCommand(command, args) {
  return new Promise((resolve, reject) => {
    const proc = spawn(command, args, { stdio: 'inherit' });
    proc.on('close', (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`Command failed with code ${code}`));
      }
    });
  });
}

async function main() {
  const args = process.argv.slice(2);
  
  if (args.length !== 2 || args[0].toUpperCase() !== 'SHOW') {
    console.log('🔥 LukeLang - Programming with Personality');
    console.log('💬 Where code meets conversation!');
    console.log('');
    console.log('Usage: luke SHOW filename.luke');
    process.exit(1);
  }

  const filePath = args[1];
  if (!filePath.endsWith('.luke')) {
    console.log('Error: File must have .luke extension');
    process.exit(1);
  }

  try {
    // Get the absolute path to main.js
    const mainJsPath = path.join(__dirname, 'main.js');
    
    // First transpile the .luke file to .js
    await runCommand('node', [mainJsPath, filePath]);
    
    // Then run the generated .js file
    const jsFilePath = filePath.replace('.luke', '.js');
    await runCommand('node', [jsFilePath]);
  } catch (error) {
    console.error('Error:', error.message);
    process.exit(1);
  }
}

main();