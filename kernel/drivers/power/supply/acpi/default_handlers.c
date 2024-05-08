/**
 * 
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <alinix/kernel.h>
#include <alinix/init.h>

#include <alinix/ucapi/namespace.h>

#define PCI_ROOT_PNP_ID "PNP0A03"
#define PCI_EXPRESS_ROOT_PNP_ID "PNP0A08"


static uacpi_namespace_node *find_pci_root(uacpi_namespace_node *node){
    static const char *pci_root_ids[] = {
        PCI_ROOT_PNP_ID,
        PCI_EXPRESS_ROOT_PNP_ID,
        NULL
    };
    uacpi_namespace_node *parent = node->parent;

    while (parent != uacpi_namespace_root()){
        
    }
}