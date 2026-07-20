import { convertEquatorialToHorizontal } from "@observerly/astrometry";
const altaz = convertEquatorialToHorizontal(new Date(), { latitude: 13.75, longitude: 100.51 }, { ra: 88.79, dec: 7.4 });
console.log(altaz);
