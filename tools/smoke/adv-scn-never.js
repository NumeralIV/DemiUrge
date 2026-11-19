// DEMIURGE — tools/smoke/adv-scn-never.js : scenario whose page never becomes ready (run by adv-robust.js).
module.exports = async (page) => {
  await page.goto('tools/smoke/adv-never.html');
  await page.waitFor('window.NEVER', 1200);
  return 'unreachable';
};
