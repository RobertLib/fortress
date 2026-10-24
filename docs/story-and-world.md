# Panství za poslední branou

Tento dokument popisuje realizovanou kampaň. Interiéry určuje
`tools/author_campaign.py`, venkovní čtvrti `tools/author_world.py`;
výsledkem jsou mapy `levels/1.txt` až `levels/5.txt`.

## Příběh a jeho pravidla

Hráč sloužil na panství jako voják. Za hladomoru otevřel panské sýpky
vesničanům. Jeho velitel jej označil za zrádce a uvěznil. Hra začíná v noci,
kdy povolí zrezivělý článek jeho řetězu. Vezme kuši a hledá cestu ven.

Panství má pět postupných uzávěrů. Nejde o pět pater jedné budovy:
vězení, podhradí, opatství, kasárna, park, pohřebiště a horní hrad tvoří
jeden uzavřený celek. Každý uzávěr ovládají tři klíče rozdělené mezi jeho
správce. Proto je hráč hledá v hlídkové kapli, hostinci, čerpací stanici,
kostele, zbrojnici, zahradníkově domku nebo u hrobu správce.

První strážce bran zemřel s přísahou, že nikoho nepropustí, dokud bude stát
poslední uzavřená brána. Pán jeho přísahu zneužil k udržení moci. Jailer je
zbytek tohoto člověka, připoutaný k celému panství. Otevřené nebe ani opuštění
vězeňské budovy samy o sobě nepředstavují únik. Sebrání klíčů jej probouzí;
střelba a hluk mu pomáhají hledat hráče. Kuše jej zadrží, ale nezabije.
Bílé svíce omezují jeho pohyb.

Otevření všech uzávěrů a odnesení posledních klíčů přes most ruší účel
přísahy. Finále je útěk, nikoli souboj s nesmrtelným protivníkem. Zvon
opatství v epilogu spojuje osobní svobodu hráče s osudem lidí, kvůli kterým
se do vězení dostal.

### Jak hráč příběh poznává

Prolog sdělí lidskou příčinu uvěznění a jednoduchý cíl. Matthias v podhradí
potvrdí, že otevření sýpek pomohlo místním. Anselm v kostele vysvětlí přísahu
a to, proč nestačí vyjít ven. Lukas v ošetřovně nasadí praktickou stopu ke
kostelnímu klíči. Pavel propojí zahrady se zásobováním vesnice. Tomáš popíše
hrob správce a cestu z pohřebiště. Benedikt připraví hráče na poslední klíč
a výstup nad trůnní síň. Každá postava patří ke svému místu a nikam se
nepřemísťuje mimo obrazovku.

Vězeňské zápisky zachovávají osobní svědectví o zvucích, zbrojích a zradě.
Rozhovory jejich nejistotu doplňují konkrétními orientačními informacemi.
Dohrání nevyžaduje přečíst každý text. Rozhovory lze opakovat; přečtení
i právě otevřený rozhovor se ukládají stejně jako zápisky. Při čtení stojí
čas, jed i nepřátelé.

Mniši jsou neútočné postavy a nepatří do počtu nepřátel ani zabití. Nejsou
eskortním úkolem. Stojí poblíž bílých svící. Svíce chrání před Jailerem;
ochrana se automaticky nevztahuje na vojáky a zvířata.

## Kapitoly a návratové trasy

| Kapitola | Povinný cíl v nové oblasti | Návrat a následující cíl |
| --- | --- | --- |
| The Outer Ward, 66 × 33 | Po útěku z cely se otevře podhradí. Studna určuje střed náměstí; třetí klíč je uvnitř hostince severovýchodně od ní. Dvě chalupy a tržiště dávají vesnici obytný charakter a skrývají zásoby. | Severní ulice se vrací na hlídkové schodiště. Vězeňská brána po třech klíčích otevře sestup pod opatství. |
| The Silent Abbey, 68 × 35 | Horní přístup z cisterny míří ke kostelu, dolní odvodňovací cesta vystoupá do klášterní zahrady. Třetí klíč leží vedle oltáře v kostele sv. Aldena. Ošetřovna a dva mniši vysvětlují místo. | Hlavní a boční kostelní dveře dovolují průchod. Zahrada se vrací na dolní okruh cisterny. Služební brána vede ke kasárnám. |
| The Garden Ward, 70 × 35 | Kasárna a zbrojnice otevřou park s trávníky, studnou a jezírkem. Klíč je v zahradníkově domku u severní větve. | Jezírko lze přejít po suchém kamenném mostku nebo obejít. Východní schodiště a jižní zásobovací cesta vedou zpět na dvě různá místa kasáren. |
| Blackwood Graves, 68 × 35 | Cesta z přijímací síně sestupuje do krypty a kostnice a vychází na hřbitov. Náhrobky a zádušní kaple předcházejí hustšímu lesu. Klíč leží u kříže na správcově lesním hrobě. | Hřbitovní cesta a lesní průsek mají dvě různá napojení na podzemí. Brána přijímací síně vede na horní hrad. |
| The Last Bell, 72 × 37 | Kaple a zbrojnice hradu vedou do otevřeného horního nádvoří. Poslední oblastní klíč je u oltáře v domě správce zvonu. | Nádvoří spojuje věžní cestu se zásobovacím přístupem zbrojnice. Finální návrat stoupá trůnní lodí do apsidy a na odkrytý úsek za poslední branou. |

V každé kapitole jsou právě tři klíče. Jeden byl přemístěn do nové čtvrti;
původní místo obsahuje šípy, takže stará odbočka stále něco nabízí. Venkovní
oblasti jsou povinnou součástí průzkumu. Každá má dva samostatné fyzické
přístupy do staršího okruhu. Žádný klíč nevyžaduje tajné dveře, teleport
ani rozbití zdi. Obě tajné skrýše a oba průlomy v každé kapitole zůstávají
nepovinnými odměnami a zkratkami.

### Rytmus, boj a zásoby

Stísněný přístup schová příští prostor, otevřené místo nabídne orientaci,
boční budova nebo skupina stromů znovu zkrátí dohled. Průchod mezi těmito
stavy je důležitější než samotná plocha mapy. Náměstí, kostel, most a hrob
jsou odlišné orientační body s různou funkcí a charakterem.

Vesnice představí ozbrojenou hlídku s houndem poblíž hostince. Opatství
střídá klidné rozhovory se stráží u zahradní cesty. V parku hlídka drží
přístup k jezírku; jeho břehy dovolují změnit směr. Hřbitov přechází od
čitelného řazení hrobů k nepravidelným skupinám stromů; rytíř hlídá cíl
a pavouk boční lesní prostor. Závěrečné nádvoří zvedá tlak kapitánem
a rytířem, ale umožňuje návrat přes druhé napojení.

Šípy doprovázejí přechody a návraty. Léčení je v obytných místech, ošetřovně
a u zahradních cest. Studny, lavice a stromy mají kolizi, ale neleží ve
dveřích ani na schodech. Trasy nevyžadují rozbíjení dekorací. Krytí před
střelbou poskytují zdi a budovy; listí stromů není neprůstřelný štít.

### Orientace a výtvarná pravidla

Obloha je noční panorama ukotvené ve směru pohledu. Budovy ji zakrývají
stropy a stěnami. Práh mezi střechou a dvorem nevytváří falešnou stěnu
až k nebi. Venkovní mlha je delší a chladnější než v interiérech, aby
byl čitelný další orientační bod.

Tráva, hlína, dlažba, štěrk a lesní podestýlka mají vlastní opakované
materiály a mipmapy. Povrch cesty je čitelný i v automapě. Název místního
orientačního bodu se objevuje v rohu pohledu; prozkoumaní mniši jsou
označeni světlým bodem. Automapa se vejde i do šířky rozšířených úrovní.

Nové čtvrti mají ručně určené vybavení. Kostel nepřebírá fontánu, jídelní
stoly a řetězy z automatického vybavování síní: vlastní oltář, kříž,
svícny, lavice, sochy a sloupy určují jeho osu. Vesnice používá hrázděné
stěny a malé obytné místnosti. Hřbitov střídá náhrobky s kříži, les hustší
skupiny jedlí a dubů kolem průseku. Malované objekty navazují na původní
materiály hry. Jejich atlas prošel i opravou oddělení jednotlivých obrázků,
aby se kolem stromů neobjevovaly útržky sousedního spritu.

Venkovní ambient používá vítr a vzdálený zvon. Interiéry zachovávají
klepání, škrábání a kroky. Boj a hon Jailerem mají před ambientem přednost.

## Ověření a hranice realizace

`ctest --test-dir build --output-on-failure` ověřuje skutečný pohyb přes
dveře, schody a sběr klíčů ve všech šesti pořadích na každé mapě; nábytek
i sudy zůstávají překážkami. `outdoor_world` navíc kontroluje otevřené nebe,
jeho zakrytí střechou, prahy, povrchy, oba přístupy do každé čtvrti,
povinný klíč, mnichy a odmítnutí nekompatibilního uložení po změně terénu
nebo objektů. Boj, pasti, obtížnosti a Jailer mají vlastní regresní testy.

`./build/world_tests --screenshots` vykreslí nové oblasti, postavy a
rozhovory do `build/world-*.bmp`. `./build/architecture_tests --screenshots`
přidá celkové plány a dveře. Snímky používají skutečný herní renderer.

Engine zůstává 2,5D raycaster: jedna podlaha a jeden strop na buňku,
kolizní kružnice a otočné malované sprity. Nejsou zde pokoje nad sebou,
skákání ani simulace denního režimu obyvatel. Studny jsou hmotné orientační
objekty bez obnovitelného léčení. Mniši mají pevně umístěné rozhovory.
Automatické testy samy o sobě neprokazují dokonalost tempa nebo obtížnosti;
ty je potřeba posuzovat i při hraní lidmi.
