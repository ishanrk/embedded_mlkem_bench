import { chromium } from '@playwright/test';
import assert from 'node:assert/strict';

const browser = await chromium.launch({
    executablePath: process.env.PQC_CHROMIUM || undefined,
    headless: true,
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const errors = [];
page.on('pageerror', error => errors.push(String(error)));
await page.goto('http://127.0.0.1:4173');
await page.getByRole('heading', { name: 'Baseline versus three instructions' }).waitFor();
assert.match(await page.locator('#results-view').innerText(), /pending fair rerun/);
await page.locator('#instruction').selectOption('fsri');
await page.locator('#shift').fill('31');
await page.waitForFunction(() => document.querySelector('#arithmetic').textContent.includes('result'));
assert.match(await page.locator('#formula').innerText(), /joined source registers/);
await page.locator('#level').selectOption('1024');
assert.match(await page.locator('#cycle-rows').innerText(), /pending rerun/);
await page.setViewportSize({ width: 390, height: 844 });
assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
assert.deepEqual(errors, []);
await browser.close();
console.log('browser walkthrough passed');
